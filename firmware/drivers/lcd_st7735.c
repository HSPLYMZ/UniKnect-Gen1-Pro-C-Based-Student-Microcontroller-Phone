#include "lcd_st7735.h"

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "board_pins.h"
#include <string.h>

/* ---------------------------------------------------------------- pins --- */

#define LCD_CS_LOW()   (LCD_CS_PORT->BSRR = (uint32_t)BOARD_PIN(LCD_CS_PIN) << 16u)
#define LCD_CS_HIGH()  (LCD_CS_PORT->BSRR = BOARD_PIN(LCD_CS_PIN))
#define LCD_DC_CMD()   (LCD_DC_PORT->BSRR = (uint32_t)BOARD_PIN(LCD_DC_PIN) << 16u)
#define LCD_DC_DATA()  (LCD_DC_PORT->BSRR = BOARD_PIN(LCD_DC_PIN))

/* 40 KB of RGB565. Lands in .bss -> SRAM1, away from the FreeRTOS heap in
 * SRAM2. Declared static so the linker keeps it in one piece. */
static uint16_t s_fb[LCD_W * LCD_H];
static unsigned s_rotation;
int lcd_width(void) { return s_rotation & 1u ? LCD_H : LCD_W; }
int lcd_height(void) { return s_rotation & 1u ? LCD_W : LCD_H; }
unsigned lcd_get_rotation(void) { return s_rotation; }
int lcd_set_rotation(unsigned rotation)
{
    if (rotation > 3u) return 0;
    s_rotation = rotation;
    lcd_fill(LCD_BLACK);
    return 1;
}

static void physical_rect(int *x, int *y, int *w, int *h)
{
    int a = *x, b = *y, width = *w, height = *h;
    switch (s_rotation) {
    case 1: *x = LCD_W - b - height; *y = a; *w = height; *h = width; break;
    case 2: *x = LCD_W - a - width; *y = LCD_H - b - height; break;
    case 3: *x = b; *y = LCD_H - a - width; *w = height; *h = width; break;
    default: break;
    }
}

/* ----------------------------------------------------------- transport --- */

/* GPIO is the recovery baseline; the application selects the board-validated
 * SPI + DMA profile. Commands, panel setup and framebuffer are shared. */
typedef enum {
    LCD_XPORT_SPI = 0,
    LCD_XPORT_BB  = 1
} lcd_xport_t;

static lcd_xport_t s_xport = LCD_XPORT_BB;
static lcd_xport_t s_next_xport = LCD_XPORT_BB;
static volatile int s_error;
static volatile int s_dma_status;
static int s_use_dma = 1;
static volatile lcd_stats_t s_stats;
static uint16_t s_transfer[LCD_W * 16];
lcd_stats_t lcd_stats(void) { return s_stats; }
void lcd_set_dma_enabled(int enabled) { s_use_dma = !!enabled; }

int lcd_get_error(void) { return s_error; }

static int spi_wait(uint32_t mask, uint32_t value)
{
    uint32_t guard = 2000000u;
    while ((SPI1->SR & mask) != value && --guard != 0u) { }
    if (guard == 0u) { s_error = LCD_ERROR_TIMEOUT; ++s_stats.timeouts; }
    return guard != 0u;
}

static int dma_stop(void)
{
    DMA2_Stream3->CR &= ~DMA_SxCR_EN;
    uint32_t guard = 10000u;
    while ((DMA2_Stream3->CR & DMA_SxCR_EN) != 0u && --guard != 0u) { }
    if (guard == 0u) { s_error = LCD_ERROR_TIMEOUT; ++s_stats.timeouts; }
    return guard != 0u;
}

void lcd_set_transport(int bitbang)
{
    s_next_xport = bitbang ? LCD_XPORT_BB : LCD_XPORT_SPI;
}

/* Defined with the bit-bang code further down; the bus dispatcher needs it. */
static void bb_byte(uint8_t b);
static void bb_gpio_mode(void);

/* --------------------------------------------------------------- delays --- */

static void lcd_delay_ms(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    /* Before the scheduler: a rough busy wait is good enough for reset timing. */
    for (volatile uint32_t i = 0; i < ms * 8000u; ++i) {
    }
}

/* ------------------------------------------------------------------ DMA --- */

/* SPI1_TX is DMA2 Stream3 channel 3; USART6_RX already owns Stream1 ch5, so
 * there is no conflict. Completion is signalled through a binary semaphore, so
 * a flush blocks the calling task while other tasks continue running. */
#define LCD_DMA_STREAM   DMA2_Stream3
#define LCD_DMA_IFCR_ALL 0x0F400000u  /* CFEIF3|CDMEIF3|CTEIF3|CHTIF3|CTCIF3 */

static SemaphoreHandle_t s_dma_done;
static StaticSemaphore_t s_dma_done_buf;

void DMA2_Stream3_IRQHandler(void)
{
    BaseType_t woken = pdFALSE;
    uint32_t flags = DMA2->LISR;
    DMA2->LIFCR = LCD_DMA_IFCR_ALL;

    uint32_t errors = flags & (DMA_LISR_TEIF3 | DMA_LISR_DMEIF3 | DMA_LISR_FEIF3);
    if (errors != 0u) { s_dma_status = LCD_ERROR_DMA; ++s_stats.dma_errors; s_stats.dma_flags=errors; }
    if ((errors != 0u || (flags & DMA_LISR_TCIF3) != 0u) && s_dma_done != NULL) {
        (void)xSemaphoreGiveFromISR(s_dma_done, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

static void dma_init(void)
{
    if (s_dma_done == NULL) {
        s_dma_done = xSemaphoreCreateBinaryStatic(&s_dma_done_buf);
    }

    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    if (!dma_stop()) return;
    LCD_DMA_STREAM->CR = 0;

    LCD_DMA_STREAM->PAR = (uint32_t)(uintptr_t)&SPI1->DR;
    /* Channel 3, memory -> peripheral, memory increment, 16-bit both sides. */
    LCD_DMA_STREAM->CR = (3u << DMA_SxCR_CHSEL_Pos) |
                         (1u << DMA_SxCR_DIR_Pos) |
                         DMA_SxCR_MINC |
                         (1u << DMA_SxCR_MSIZE_Pos) |
                         (1u << DMA_SxCR_PSIZE_Pos) |
                         DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    /* FIFO is explicitly enabled when using its error interrupt. Prefetching
     * avoids direct-mode underruns when SPI requests arrive before SRAM data. */
    LCD_DMA_STREAM->FCR = DMA_SxFCR_DMDIS | DMA_SxFCR_FTH | DMA_SxFCR_FEIE;

    /* Must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5)
     * because the handler calls a FreeRTOS FromISR API. */
    NVIC_SetPriority(DMA2_Stream3_IRQn, 5);
    NVIC_EnableIRQ(DMA2_Stream3_IRQn);
}

/* ------------------------------------------------------------------ SPI --- */

static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN | RCC_AHB1ENR_GPIODEN | RCC_AHB1ENR_GPIOFEN;
    (void)RCC->AHB1ENR;

    /* PA5 SCK, PA6 MISO, PA7 MOSI -> AF5, very high speed. */
    uint32_t moder = GPIOA->MODER;
    moder &= ~((3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_MISO_PIN * 2u)) |
               (3u << (LCD_MOSI_PIN * 2u)));
    moder |= ((2u << (LCD_SCK_PIN * 2u)) | (2u << (LCD_MISO_PIN * 2u)) |
              (2u << (LCD_MOSI_PIN * 2u)));
    GPIOA->MODER = moder;

    GPIOA->AFR[0] &= ~((0xFu << (LCD_SCK_PIN * 4u)) | (0xFu << (LCD_MISO_PIN * 4u)) |
                       (0xFu << (LCD_MOSI_PIN * 4u)));
    GPIOA->AFR[0] |= ((5u << (LCD_SCK_PIN * 4u)) | (5u << (LCD_MISO_PIN * 4u)) |
                      (5u << (LCD_MOSI_PIN * 4u)));

    GPIOA->OSPEEDR |= ((3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_MOSI_PIN * 2u)));

    /* Preload idle levels before enabling the control outputs. */
    LCD_CS_HIGH();
    LCD_DC_DATA();
    GPIOD->OTYPER &= ~BOARD_PIN(LCD_CS_PIN);
    GPIOF->OTYPER &= ~BOARD_PIN(LCD_DC_PIN);
    GPIOA->OTYPER &= ~(BOARD_PIN(LCD_SCK_PIN) | BOARD_PIN(LCD_MOSI_PIN));

    /* CS (PD14), DC (PF12) as push-pull outputs, initially high.
     * Leave PD15 to the modem reset owner in board_init(). */
    uint32_t pd_moder = GPIOD->MODER;
    pd_moder &= ~(3u << (LCD_CS_PIN * 2u));
    pd_moder |= (1u << (LCD_CS_PIN * 2u));
    GPIOD->MODER = pd_moder;
    GPIOD->OSPEEDR |= (3u << (LCD_CS_PIN * 2u));

    uint32_t pf_moder = GPIOF->MODER;
    pf_moder &= ~(3u << (LCD_DC_PIN * 2u));
    pf_moder |= (1u << (LCD_DC_PIN * 2u));
    GPIOF->MODER = pf_moder;
    GPIOF->OSPEEDR |= (3u << (LCD_DC_PIN * 2u));

    LCD_CS_HIGH();
    LCD_DC_DATA();

    if (s_xport == LCD_XPORT_BB) {
        bb_gpio_mode();
    }
}

/* SPI prescaler field (CR1 BR[2:0]): 0=/2 1=/4 2=/8 3=/16 4=/32 5=/64 6=/128 7=/256.
 * APB2 is 100 MHz, so /16 = 6.25 MHz and /64 = 1.56 MHz. Runtime rather than
 * compile-time because bring-up needs to compare speeds without a rebuild, and
 * because the shield level-shifts every line through a CD4050 whose edge rate
 * is the prime suspect when fast fails and slow works. */
static uint32_t s_spi_br = 3u;

void lcd_set_spi_div(uint32_t br_field)
{
    s_spi_br = br_field & 7u;
}

static void spi_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;

    RCC->APB2RSTR |= RCC_APB2RSTR_SPI1RST;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_SPI1RST;
    SPI1->CR1 = 0;
    /* Master, 8-bit, mode 0, software NSS, prescaler from s_spi_br. */
    SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI |
                SPI_CR1_BIDIMODE | SPI_CR1_BIDIOE |
                (s_spi_br << SPI_CR1_BR_Pos);
    SPI1->CR2 = 0;
    SPI1->CR1 |= SPI_CR1_SPE;
}

static void spi_write8(const uint8_t *buf, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (!spi_wait(SPI_SR_TXE, SPI_SR_TXE)) return;
        /* Match the 8-bit command frame with an explicit byte access. */
        *(__IO uint8_t *)&SPI1->DR = buf[i];
    }
    if (!spi_wait(SPI_SR_TXE, SPI_SR_TXE)) return;
    (void)spi_wait(SPI_SR_BSY, 0);
}

/* One 16-bit frame per pixel. ST7735 wants big-endian RGB565, which is exactly
 * what the SPI shift register emits in 16-bit mode.
 *
 * Pre-scheduler callers get a polling path: a semaphore take is not legal until
 * the scheduler runs, and init happens before any task exists. */
static void spi_write16(const uint16_t *px, size_t n)
{
    if (n == 0u) {
        return;
    }

    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 |= SPI_CR1_DFF;
    SPI1->CR1 |= SPI_CR1_SPE;

    if (!s_use_dma || xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) {
        for (size_t i = 0; i < n; ++i) {
            if (!spi_wait(SPI_SR_TXE, SPI_SR_TXE)) goto cleanup;
            SPI1->DR = px[i];
        }
    } else {
        if (!dma_stop()) goto cleanup;
        NVIC_DisableIRQ(DMA2_Stream3_IRQn);
        DMA2->LIFCR = LCD_DMA_IFCR_ALL;
        NVIC_ClearPendingIRQ(DMA2_Stream3_IRQn);
        while (xSemaphoreTake(s_dma_done, 0) == pdTRUE) { }
        s_dma_status = LCD_ERROR_NONE;
        LCD_DMA_STREAM->M0AR = (uint32_t)(uintptr_t)px;
        LCD_DMA_STREAM->NDTR = (uint16_t)n; /* NDTR is 16-bit: 65535 px max */

        NVIC_EnableIRQ(DMA2_Stream3_IRQn);
        LCD_DMA_STREAM->CR |= DMA_SxCR_EN;
        __DSB();
        SPI1->CR2 |= SPI_CR2_TXDMAEN;

        uint32_t timeout_ms = (uint32_t)(((uint64_t)n * 16u * 1000u *
                              (2u << s_spi_br) + SystemCoreClock - 1u) /
                              SystemCoreClock) + 50u;
        if (xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            s_error = LCD_ERROR_TIMEOUT; ++s_stats.timeouts;
        }
        else if (s_dma_status != LCD_ERROR_NONE) s_error = s_dma_status;
    }

cleanup:
    SPI1->CR2 &= ~SPI_CR2_TXDMAEN;
    (void)dma_stop();
    if (s_error == LCD_ERROR_NONE) {
        (void)spi_wait(SPI_SR_TXE, SPI_SR_TXE);
        (void)spi_wait(SPI_SR_BSY, 0);
    }

    SPI1->CR1 &= ~SPI_CR1_SPE;
    SPI1->CR1 &= ~SPI_CR1_DFF;
    SPI1->CR1 |= SPI_CR1_SPE;
}

/* All commands and pixels must use the same transport. RGB565 pixels are
 * always sent most-significant byte first, independent of CPU byte order. */
static void bus_write8(const uint8_t *data, size_t n)
{
    if (s_error != LCD_ERROR_NONE) return;
    if (s_xport == LCD_XPORT_SPI) {
        spi_write8(data, n);
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        bb_byte(data[i]);
    }
}

static void bus_write16(const uint16_t *pixels, size_t n)
{
    if (s_error != LCD_ERROR_NONE) return;
    ++s_stats.transfers;
    if (s_xport == LCD_XPORT_SPI) {
        spi_write16(pixels, n);
        if (s_error == LCD_ERROR_NONE) s_stats.pixels += (uint32_t)n;
        return;
    }
    for (size_t i = 0; i < n; ++i) {
        bb_byte((uint8_t)(pixels[i] >> 8));
        bb_byte((uint8_t)pixels[i]);
    }
    s_stats.pixels += (uint32_t)n;
}

/* ------------------------------------------------------------- commands --- */

/* Leave a settling gap after changing CS/DC. This precaution did not resolve
 * the hardware-SPI failure and must not be treated as its established cause. */
static void lcd_bus_settle(void)
{
    for (volatile int i = 0; i < 40; ++i) {
    }
}

static void lcd_cmd(uint8_t cmd)
{
    LCD_CS_LOW();
    LCD_DC_CMD();
    lcd_bus_settle();
    bus_write8(&cmd, 1);
    LCD_CS_HIGH();
}

static void lcd_data(const uint8_t *data, size_t n)
{
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_bus_settle();
    bus_write8(data, n);
    LCD_CS_HIGH();
}

static void lcd_set_window(int x, int y, int w, int h)
{
    uint16_t xs = (uint16_t)(x + LCD_COL_OFFSET);
    uint16_t xe = (uint16_t)(x + w - 1 + LCD_COL_OFFSET);
    uint16_t ys = (uint16_t)(y + LCD_ROW_OFFSET);
    uint16_t ye = (uint16_t)(y + h - 1 + LCD_ROW_OFFSET);

    uint8_t caset[4] = {(uint8_t)(xs >> 8), (uint8_t)xs,
                        (uint8_t)(xe >> 8), (uint8_t)xe};
    uint8_t raset[4] = {(uint8_t)(ys >> 8), (uint8_t)ys,
                        (uint8_t)(ye >> 8), (uint8_t)ye};

    lcd_cmd(0x2A);
    lcd_data(caset, 4);
    lcd_cmd(0x2B);
    lcd_data(raset, 4);
    lcd_cmd(0x2C);
}

/* ----------------------------------------------------------- init table --- */

void lcd_init(void)
{
    s_error = LCD_ERROR_NONE;
    s_stats=(lcd_stats_t){0};
    s_rotation = 0;
    s_xport = s_next_xport;
    /* Disable a previously selected SPI transport before taking GPIO control.
     * The GPIO path does not start SPI or allocate/configure a DMA stream. */
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    (void)RCC->APB2ENR;
    SPI1->CR2 = 0;
    SPI1->CR1 = 0;
    gpio_init();
    if (s_xport == LCD_XPORT_SPI) {
        spi_init();
        dma_init();
    }

    /* Let the panel's own power-on circuit settle before the first command. */
    lcd_delay_ms(120);

    lcd_cmd(0x01); /* SWRESET */
    lcd_delay_ms(150);
    lcd_cmd(0x11); /* SLPOUT */
    lcd_delay_ms(500);

    static const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frmctr2[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const uint8_t invctr[]  = {0x07};
    static const uint8_t pwctr1[]  = {0xA2, 0x02, 0x84};
    static const uint8_t pwctr2[]  = {0xC5};
    static const uint8_t pwctr3[]  = {0x0A, 0x00};
    static const uint8_t pwctr4[]  = {0x8A, 0x2A};
    static const uint8_t pwctr5[]  = {0x8A, 0xEE};
    static const uint8_t vmctr1[]  = {0x0E};
    /* 0x60 = MV|MX: landscape 160x128. MV is mandatory, not cosmetic -- without
     * it the X axis is limited to the controller's 132 columns and a 160 px
     * window overflows. MX mirrors the result horizontally; drop it if the test
     * pattern's top-left corner shows up on the right. */
    static const uint8_t madctl[]  = {0x60};
    static const uint8_t colmod[]  = {0x05}; /* 16 bit/px */
    static const uint8_t gmctrp1[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                      0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    static const uint8_t gmctrn1[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D,
                                      0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};

    lcd_cmd(0xB1); lcd_data(frmctr1, sizeof frmctr1);
    lcd_cmd(0xB2); lcd_data(frmctr2, sizeof frmctr2);
    lcd_cmd(0xB3); lcd_data(frmctr3, sizeof frmctr3);
    lcd_cmd(0xB4); lcd_data(invctr, sizeof invctr);
    lcd_cmd(0xC0); lcd_data(pwctr1, sizeof pwctr1);
    lcd_cmd(0xC1); lcd_data(pwctr2, sizeof pwctr2);
    lcd_cmd(0xC2); lcd_data(pwctr3, sizeof pwctr3);
    lcd_cmd(0xC3); lcd_data(pwctr4, sizeof pwctr4);
    lcd_cmd(0xC4); lcd_data(pwctr5, sizeof pwctr5);
    lcd_cmd(0xC5); lcd_data(vmctr1, sizeof vmctr1);
#if LCD_INVERT
    lcd_cmd(0x21); /* INVON: panel is inverted */
#else
    lcd_cmd(0x20); /* INVOFF */
#endif
    lcd_cmd(0x36); lcd_data(madctl, sizeof madctl);
    lcd_cmd(0x3A); lcd_data(colmod, sizeof colmod);
    lcd_cmd(0xE0); lcd_data(gmctrp1, sizeof gmctrp1);
    lcd_cmd(0xE1); lcd_data(gmctrn1, sizeof gmctrn1);
    lcd_cmd(0x13); /* NORON */
    lcd_delay_ms(10);
    lcd_cmd(0x29); /* DISPON */
    lcd_delay_ms(100);

    lcd_fill(LCD_BLACK);
    lcd_flush();
}

/* ------------------------------------------------------------ drawing --- */

void lcd_display_on(void)
{
    lcd_cmd(0x29); /* DISPON */
}

void lcd_display_off(void)
{
    lcd_cmd(0x28); /* DISPOFF */
}

uint32_t lcd_read_id(void)
{
    /* This shield has no LCD readback connection. Do not wait on an inactive
     * SPI peripheral when the GPIO transport owns PA5/PA7. */
    if (s_xport == LCD_XPORT_BB || (SPI1->CR1 & SPI_CR1_BIDIMODE)) {
        return 0;
    }
    uint8_t cmd = 0x04; /* RDDID */
    uint8_t buf[4] = {0, 0, 0, 0};

    LCD_CS_LOW();
    LCD_DC_CMD();
    lcd_bus_settle();
    spi_write8(&cmd, 1);

    /* Drop whatever the command phase clocked in. */
    (void)SPI1->DR;
    (void)SPI1->SR; /* clear RXNE/OVR left by write-only transfers */

    LCD_DC_DATA();
    lcd_bus_settle();
    for (int i = 0; i < 4; ++i) {
        if (!spi_wait(SPI_SR_TXE, SPI_SR_TXE)) break;
        SPI1->DR = 0xFF;
        if (!spi_wait(SPI_SR_RXNE, SPI_SR_RXNE)) break;
        buf[i] = (uint8_t)(SPI1->DR & 0xFFu);
    }
    LCD_CS_HIGH();

    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

void lcd_fill_direct(uint16_t color, int w, int h)
{
    static uint16_t line[LCD_W];

    if (w <= 0 || h <= 0) {
        return;
    }
    if (w > lcd_width()) {
        w = lcd_width();
    }
    if (h > lcd_height()) {
        h = lcd_height();
    }
    int x = 0, y = 0;
    physical_rect(&x, &y, &w, &h);
    /* In portrait mode the physical row length is the logical height. Fill
     * after mapping so the entire transmitted row has the requested color. */
    for (int i = 0; i < w; ++i) {
        line[i] = color;
    }
    lcd_set_window(x, y, w, h);
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_bus_settle();
    for (int row = 0; row < h; ++row) {
        bus_write16(line, (size_t)w);
    }
    LCD_CS_HIGH();
}

void lcd_draw_pixel(int x, int y, uint16_t color)
{
    if ((unsigned)x >= (unsigned)lcd_width() || (unsigned)y >= (unsigned)lcd_height()) {
        return;
    }
    int w = 1, h = 1;
    physical_rect(&x, &y, &w, &h);
    s_fb[(size_t)y * LCD_W + (size_t)x] = color;
}

void lcd_draw_cell(int x,int y,const uint8_t rows[8],uint16_t fg,uint16_t bg)
{
    if(x<0 || y<0 || x>lcd_width()-6 || y>lcd_height()-8) return;
    static const int dx[4]={1,LCD_W,-1,-LCD_W};
    static const int dy[4]={LCD_W,-1,-LCD_W,1};
    int w=1,h=1; physical_rect(&x,&y,&w,&h);
    int start=y*LCD_W+x;
    for(int row=0;row<8;++row) {
        int offset=start+row*dy[s_rotation];
        for(int col=0;col<6;++col)
            s_fb[offset+col*dx[s_rotation]]=(rows[row]&(32u>>col))?fg:bg;
    }
}

static int clip_rect(int *x, int *y, int *w, int *h)
{
    if (*w <= 0 || *h <= 0) return 0;
    int64_t right = (int64_t)*x + *w;
    int64_t bottom = (int64_t)*y + *h;
    if (*x >= lcd_width() || *y >= lcd_height() || right <= 0 || bottom <= 0) return 0;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (right > lcd_width()) right = lcd_width();
    if (bottom > lcd_height()) bottom = lcd_height();
    *w = (int)(right - *x);
    *h = (int)(bottom - *y);
    return *w > 0 && *h > 0;
}

void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (!clip_rect(&x, &y, &w, &h)) return;
    physical_rect(&x, &y, &w, &h);
    for (int row = y; row < y + h; ++row) {
        uint16_t *p = &s_fb[(size_t)row * LCD_W + (size_t)x];
        for (int col = 0; col < w; ++col) {
            *p++ = color;
        }
    }
}

void lcd_frame_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    int64_t right = (int64_t)x + w - 1;
    int64_t bottom = (int64_t)y + h - 1;
    if (y >= 0 && y < lcd_height()) lcd_fill_rect(x, y, w, 1, color);
    if (bottom >= 0 && bottom < lcd_height()) lcd_fill_rect(x, (int)bottom, w, 1, color);
    if (x >= 0 && x < lcd_width()) lcd_fill_rect(x, y, 1, h, color);
    if (right >= 0 && right < lcd_width()) lcd_fill_rect((int)right, y, 1, h, color);
}

void lcd_fill(uint16_t color)
{
    for (size_t i = 0; i < (size_t)LCD_W * LCD_H; ++i) {
        s_fb[i] = color;
    }
}

/* -------------------------------------------------------------- flush --- */

void lcd_flush(void)
{
    lcd_set_window(0, 0, LCD_W, LCD_H);
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_bus_settle();
    bus_write16(s_fb, (size_t)LCD_W * LCD_H);
    LCD_CS_HIGH();
}

void lcd_flush_rect(int x, int y, int w, int h)
{
    if (!clip_rect(&x, &y, &w, &h)) return;
    physical_rect(&x, &y, &w, &h);

    lcd_set_window(x, y, w, h);

    /* CS stays low across the whole rectangle: RAMWR is a continuous pixel
     * stream and toggling CS mid-stream can desynchronise the address counter. */
    LCD_CS_LOW();
    LCD_DC_DATA();
    lcd_bus_settle();
    if (w == LCD_W) {
        bus_write16(&s_fb[(size_t)y * LCD_W], (size_t)w * (size_t)h);
    } else for (int row = 0; row < h && s_error == LCD_ERROR_NONE; ) {
        int rows = h-row; if (rows > 16) rows = 16;
        for (int i = 0; i < rows; ++i)
            memcpy(&s_transfer[i*w], &s_fb[(size_t)(y+row+i)*LCD_W+(size_t)x], (size_t)w*2u);
        bus_write16(s_transfer, (size_t)w * (size_t)rows);
        row += rows;
    }
    LCD_CS_HIGH();
}

/* ---------------------------------------------------------- GPIO SPI --- */

/* Mode 0, MSB first. Conservative timing remains available for recovery. */

/* Half-bit delay, in volatile loop iterations. At 100 MHz each iteration costs
 * roughly 5 cycles, so 12 gives about 0.6 us (~800 kHz) and 100 gives about
 * 5 us (~100 kHz). Slow mode exists because the shield pushes every signal
 * through a CD4050 level shifter, and a marginal one passes slow edges while
 * smearing fast ones. */
static uint32_t s_bb_half = 12u;
void lcd_set_gpio_delay(uint32_t iterations) { s_bb_half = iterations > 100u ? 100u : iterations; }

static void bb_delay(void)
{
    for (volatile uint32_t i = 0; i < s_bb_half; ++i) {
    }
}

static void bb_gpio_mode(void)
{
    uint32_t m = GPIOA->MODER;
    m &= ~((3u << (LCD_SCK_PIN * 2u)) | (3u << (LCD_MOSI_PIN * 2u)));
    m |= ((1u << (LCD_SCK_PIN * 2u)) | (1u << (LCD_MOSI_PIN * 2u)));
    GPIOA->BSRR = (uint32_t)BOARD_PIN(LCD_SCK_PIN) << 16u; /* SCK idle low */
    GPIOA->MODER = m;
}

static void bb_byte(uint8_t b)
{
    if (s_bb_half == 0u) {
        /* One BSRR write establishes MOSI and SCK low, then raises SCK.
         * Validate this fast profile visually before selecting it. */
        const uint32_t clock = BOARD_PIN(LCD_SCK_PIN), data = BOARD_PIN(LCD_MOSI_PIN);
        for (unsigned i = 0; i < 8; ++i) {
            GPIOA->BSRR = (clock << 16u) | ((b & 0x80u) ? data : (data << 16u));
            __NOP();
            GPIOA->BSRR = clock;
            __NOP();
            b <<= 1;
        }
        GPIOA->BSRR = clock << 16u;
        return;
    }
    for (int i = 7; i >= 0; --i) {
        if ((b & (1u << i)) != 0u) {
            GPIOA->BSRR = BOARD_PIN(LCD_MOSI_PIN);
        } else {
            GPIOA->BSRR = (uint32_t)BOARD_PIN(LCD_MOSI_PIN) << 16u;
        }
        bb_delay();
        GPIOA->BSRR = BOARD_PIN(LCD_SCK_PIN);                  /* rising edge */
        bb_delay();
        GPIOA->BSRR = (uint32_t)BOARD_PIN(LCD_SCK_PIN) << 16u; /* falling edge */
    }
    /* The next bit already waits after setting MOSI while SCK is low.
     * Keep a final low interval before CS/DC can change at byte end. */
    bb_delay();
}

/* Compatibility helpers use the same init/window/pixel path as normal UI
 * operations. The selected GPIO transport remains active afterwards. */
static void bitbang_fill(uint16_t color, uint32_t half)
{
    s_bb_half = half;
    lcd_set_transport(1);
    lcd_init();
    lcd_fill_direct(color, LCD_W, LCD_H);
}

void lcd_bitbang_fill(uint16_t color)
{
    bitbang_fill(color, 12u);
}

void lcd_bitbang_fill_slow(uint16_t color)
{
    bitbang_fill(color, 100u);
}

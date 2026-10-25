#include "common.h"
#include <limits.h>
#include "../../firmware/drivers/lcd_st7735.c"
extern BaseType_t test_scheduler;
void test_main(void)
{
    lcd_fill(LCD_BLACK);
    lcd_frame_rect(10, 10, 0, 5, LCD_RED);
    lcd_frame_rect(10, 10, 5, -1, LCD_RED);
    for (unsigned i = 0; i < LCD_W * LCD_H; ++i) CHECK(s_fb[i] == LCD_BLACK);
    lcd_fill_rect(-2, -2, 4, 4, LCD_GREEN);
    for (int y = 0; y < LCD_H; ++y)
        for (int x = 0; x < LCD_W; ++x)
            CHECK(s_fb[y * LCD_W + x] == (x < 2 && y < 2 ? LCD_GREEN : LCD_BLACK));
    lcd_fill_rect(INT_MAX, 0, INT_MAX, 2, LCD_RED);
    lcd_fill_rect(INT_MIN, 0, INT_MAX, 2, LCD_RED);
    lcd_flush_rect(INT_MAX, INT_MIN, INT_MAX, INT_MAX);
    CHECK(s_fb[0] == LCD_GREEN);
    for (unsigned r = 0; r < 4; ++r) {
        CHECK(lcd_set_rotation(r));
        CHECK(lcd_width() == (r & 1 ? 128 : 160));
        CHECK(lcd_height() == (r & 1 ? 160 : 128));
        lcd_draw_pixel(0,0,LCD_RED);
        lcd_draw_pixel(lcd_width()-1,0,LCD_GREEN);
        lcd_draw_pixel(0,lcd_height()-1,LCD_BLUE);
        lcd_draw_pixel(lcd_width()-1,lcd_height()-1,LCD_WHITE);
        static const uint16_t tl[] = {LCD_RED,LCD_BLUE,LCD_WHITE,LCD_GREEN};
        static const uint16_t tr[] = {LCD_GREEN,LCD_RED,LCD_BLUE,LCD_WHITE};
        CHECK(s_fb[0] == tl[r] && s_fb[LCD_W-1] == tr[r]);
        lcd_fill(LCD_BLACK);
        lcd_fill_rect(-1,-1,3,3,LCD_YELLOW);
        unsigned count = 0;
        for (unsigned i = 0; i < LCD_W*LCD_H; ++i) if (s_fb[i] == LCD_YELLOW) ++count;
        CHECK(count == 4);
        int x = lcd_width()-2, y = lcd_height()-3, w = 2, h = 3;
        physical_rect(&x,&y,&w,&h);
        CHECK(x >= 0 && y >= 0 && x+w <= LCD_W && y+h <= LCD_H && w*h == 6);
        x = 3; y = 4; w = 5; h = 6;
        static const int expected[4][4] = {{3,4,5,6},{150,3,6,5},{152,118,5,6},{4,120,6,5}};
        physical_rect(&x,&y,&w,&h);
        CHECK(x == expected[r][0] && y == expected[r][1] && w == expected[r][2] && h == expected[r][3]);
        static const uint8_t glyph[8]={32,16,8,4,2,1,63,21};
        lcd_fill(LCD_BLACK);
        lcd_draw_cell(5,7,glyph,LCD_WHITE,LCD_BLUE);
        uint16_t saved[48];
        for(unsigned row=0;row<8;++row) for(unsigned col=0;col<6;++col) {
            int cx=5+(int)col,cy=7+(int)row,cw=1,ch=1;
            physical_rect(&cx,&cy,&cw,&ch);
            saved[row*6+col]=s_fb[cy*LCD_W+cx];
            CHECK(saved[row*6+col]==((glyph[row]&(32u>>col))?LCD_WHITE:LCD_BLUE));
        }
        lcd_draw_cell(-1,0,glyph,LCD_RED,LCD_RED); /* complete cells only */
        lcd_fill_rect(INT_MAX,INT_MIN,INT_MAX,INT_MAX,LCD_RED);
    }
    CHECK(!lcd_set_rotation(4));
    /* Exercise the actual polling SPI transfer rather than only its geometry.
     * With TXE ready and BSY clear, DR contains the last physical pixel sent.
     * Portrait fills used to initialize 128 entries but transmit 160, leaving
     * the row tail at the previous color. Unequal partial sizes catch the same
     * problem without depending on the panel's full dimensions. */
    test_scheduler = taskSCHEDULER_NOT_STARTED;
    s_xport = LCD_XPORT_SPI;
    SPI1->SR = SPI_SR_TXE;
    static const uint16_t fills[] = {LCD_RED,LCD_GREEN,LCD_BLUE,LCD_WHITE};
    for (unsigned r = 0; r < 4; ++r) {
        CHECK(lcd_set_rotation(r));
        lcd_fill_direct(fills[r],lcd_width(),lcd_height());
        CHECK(SPI1->DR == fills[r]);
        lcd_fill_direct(LCD_YELLOW,2,7);
        CHECK(SPI1->DR == LCD_YELLOW);
    }
    test_scheduler = taskSCHEDULER_RUNNING;
    s_xport = LCD_XPORT_BB;
    CHECK(lcd_set_rotation(0));
    lcd_fill(LCD_BLACK);
    lcd_frame_rect(-1, -1, 4, 4, LCD_WHITE);
    CHECK(s_fb[0] == LCD_BLACK);
    CHECK(s_fb[2] == LCD_WHITE && s_fb[2 * LCD_W] == LCD_WHITE);
    uint8_t b = 0;
    SPI1->SR = 0;
    spi_write8(&b, 1);
    CHECK(lcd_get_error() == LCD_ERROR_TIMEOUT);
    s_error = LCD_ERROR_NONE;
    s_dma_done = (SemaphoreHandle_t)1;
    uint16_t px = LCD_RED;
    spi_write16(&px, 1); /* fake RTOS times out instead of waiting forever */
    CHECK(lcd_get_error() == LCD_ERROR_TIMEOUT);
    CHECK((SPI1->CR2 & SPI_CR2_TXDMAEN) == 0);
    CHECK((LCD_DMA_STREAM->CR & DMA_SxCR_EN) == 0);
    /* Packed narrow rectangles start a transfer per 16 physical rows, not per row. */
    s_error=LCD_ERROR_NONE;
    lcd_set_dma_enabled(0);
    s_xport=LCD_XPORT_SPI;
    SPI1->SR=SPI_SR_TXE;
    CHECK(lcd_set_rotation(0));
    for(int y=0;y<LCD_H;++y) for(int x=0;x<LCD_W;++x)
        s_fb[y*LCD_W+x]=(uint16_t)(y*LCD_W+x);
    lcd_stats_t before=lcd_stats();
    lcd_flush_rect(3,4,10,17);
    CHECK(lcd_get_error()==LCD_ERROR_NONE);
    CHECK(lcd_stats().transfers-before.transfers==2 && lcd_stats().pixels-before.pixels==170);
    for(unsigned i=0;i<10;++i) CHECK(s_transfer[i]==(uint16_t)(20*LCD_W+3+i));
    for(unsigned i=10;i<160;++i) CHECK(s_transfer[i]==(uint16_t)((4+i/10)*LCD_W+3+i%10));
    CHECK(SPI1->DR==20*LCD_W+12);
    CHECK(!(SPI1->CR1 & SPI_CR1_DFF));
    before=lcd_stats(); lcd_flush_rect(0,0,LCD_W,20);
    CHECK(lcd_stats().transfers-before.transfers==1 && lcd_stats().pixels-before.pixels==LCD_W*20);
    lcd_set_dma_enabled(1);
    extern unsigned test_semaphore_successes;
    dma_init();
    CHECK((LCD_DMA_STREAM->FCR & (DMA_SxFCR_DMDIS|DMA_SxFCR_FTH|DMA_SxFCR_FEIE)) ==
          (DMA_SxFCR_DMDIS|DMA_SxFCR_FTH|DMA_SxFCR_FEIE));
    test_semaphore_successes=1;
    SPI1->SR=SPI_SR_TXE;
    spi_write16(s_transfer,16);
    CHECK(lcd_get_error()==LCD_ERROR_NONE && !test_semaphore_successes);
    CHECK(LCD_DMA_STREAM->NDTR==16);
    CHECK(LCD_DMA_STREAM->M0AR==(uint32_t)(uintptr_t)s_transfer);
    CHECK(!(LCD_DMA_STREAM->CR & DMA_SxCR_EN) && !(SPI1->CR2 & SPI_CR2_TXDMAEN));
    CHECK(!(SPI1->CR1 & SPI_CR1_DFF));
    s_dma_status=LCD_ERROR_NONE;
    DMA2->LISR=DMA_LISR_TCIF3;
    DMA2_Stream3_IRQHandler();
    CHECK(s_dma_status==LCD_ERROR_NONE);
    DMA2->LISR = DMA_LISR_TEIF3;
    DMA2_Stream3_IRQHandler();
    CHECK(s_dma_status == LCD_ERROR_DMA);
    test_finish(0);
}

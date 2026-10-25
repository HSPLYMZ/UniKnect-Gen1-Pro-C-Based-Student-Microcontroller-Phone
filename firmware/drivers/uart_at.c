#include "uart_at.h"

#include <stddef.h>

#include "board_pins.h"
#include "hal_uart.h"
#include "ringbuffer.h"

#if (AT_UART_RB_SIZE & (AT_UART_RB_SIZE - 1u)) != 0u
#error "AT_UART_RB_SIZE must be a power of two"
#endif

static uint8_t s_dma_buf[AT_UART_DMA_SIZE];
static uint8_t s_rb_buf[AT_UART_RB_SIZE];
static ringbuffer_t s_rb;

/* Producer-side cursor: how far the DMA has written, tracked by us because a
 * circular transfer only exposes the remaining count. */
static volatile uint16_t s_dma_pos;
static volatile event_callback_t s_rx_cb;
static volatile at_uart_stats_t s_stats;
static uint8_t s_ready;

/* ------------------------------------------------------------------ init --- */

static void gpio_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOGEN;
    (void)RCC->AHB1ENR;

    /* RX/TX as alternate function, push-pull, very high speed. */
    uint32_t moder = MODEM_TX_PORT->MODER;
    moder &= ~((3u << (MODEM_TX_PIN * 2u)) | (3u << (MODEM_RX_PIN * 2u)));
    moder |= ((2u << (MODEM_TX_PIN * 2u)) | (2u << (MODEM_RX_PIN * 2u)));
    MODEM_TX_PORT->MODER = moder;

    uint32_t ospeedr = MODEM_TX_PORT->OSPEEDR;
    ospeedr |= (3u << (MODEM_TX_PIN * 2u)) | (3u << (MODEM_RX_PIN * 2u));
    MODEM_TX_PORT->OSPEEDR = ospeedr;

    /* PG9..PG15 live in AFR[1]; both pins are AF8 (USART6). */
    MODEM_TX_PORT->AFR[1] &= ~((0xFu << ((MODEM_TX_PIN - 8u) * 4u)) |
                               (0xFu << ((MODEM_RX_PIN - 8u) * 4u)));
    MODEM_TX_PORT->AFR[1] |= ((8u << ((MODEM_TX_PIN - 8u) * 4u)) |
                              (8u << ((MODEM_RX_PIN - 8u) * 4u)));

    /* Idle line should read high; a floating RX pin produces phantom 0x00. */
    uint32_t pupdr = MODEM_TX_PORT->PUPDR;
    pupdr &= ~(3u << (MODEM_RX_PIN * 2u));
    pupdr |= (1u << (MODEM_RX_PIN * 2u));
    MODEM_TX_PORT->PUPDR = pupdr;
}

static void dma_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    /* USART6_RX is DMA2 Stream1, channel 5. */
    DMA2_Stream1->CR = 0;
    while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0u) {
    }

    DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 |
                  DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;

    DMA2_Stream1->PAR = (uint32_t)(uintptr_t)&MODEM_UART->DR;
    DMA2_Stream1->M0AR = (uint32_t)(uintptr_t)s_dma_buf;
    DMA2_Stream1->NDTR = AT_UART_DMA_SIZE;
    DMA2_Stream1->FCR = 0;

    DMA2_Stream1->CR = (5u << DMA_SxCR_CHSEL_Pos)  /* channel 5 */
                     | DMA_SxCR_MINC               /* memory increment */
                     | DMA_SxCR_CIRC               /* circular */
                     | DMA_SxCR_HTIE | DMA_SxCR_TCIE
                     | DMA_SxCR_TEIE | DMA_SxCR_DMEIE;
    DMA2_Stream1->FCR = DMA_SxFCR_FEIE;

    /* Same priority as USART6: drain_dma() must never preempt itself. */
    NVIC_SetPriority(DMA2_Stream1_IRQn, 5);
    NVIC_ClearPendingIRQ(DMA2_Stream1_IRQn);
    NVIC_EnableIRQ(DMA2_Stream1_IRQn);

    DMA2_Stream1->CR |= DMA_SxCR_EN;
}

void at_uart_init(void)
{
    if (s_ready) {
        return;
    }
    s_ready = 1;

    rb_init(&s_rb, s_rb_buf, AT_UART_RB_SIZE);
    s_dma_pos = 0;

    RCC->APB2ENR |= RCC_APB2ENR_USART6EN;
    (void)RCC->APB2ENR;

    gpio_init();
    dma_init();

    /* APB2 prescaler is /1, so PCLK2 == SystemCoreClock (100 MHz). */
    MODEM_UART->CR1 = 0;
    MODEM_UART->CR2 = 0;
    MODEM_UART->CR3 = USART_CR3_DMAR;
    MODEM_UART->BRR = (SystemCoreClock + MODEM_BAUDRATE / 2u) / MODEM_BAUDRATE;
    MODEM_UART->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE | USART_CR1_IDLEIE;

    /* Must be numerically >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5),
     * because the ISR releases a FreeRTOS semaphore. */
    NVIC_SetPriority(MODEM_UART_IRQn, 5);
    NVIC_EnableIRQ(MODEM_UART_IRQn);
}

/* ------------------------------------------------------------ RX plumbing -- */

static void drain_dma(void)
{
    uint16_t remaining = (uint16_t)DMA2_Stream1->NDTR;
    uint16_t pos = (uint16_t)(AT_UART_DMA_SIZE - remaining);
    if (pos == AT_UART_DMA_SIZE) {
        pos = 0; /* NDTR can briefly be zero before circular reload. */
    }

    if (pos == s_dma_pos) {
        return;
    }

    uint16_t wrote = 0;
    if (pos > s_dma_pos) {
        wrote = rb_put(&s_rb, &s_dma_buf[s_dma_pos], (uint16_t)(pos - s_dma_pos));
    } else {
        wrote = rb_put(&s_rb, &s_dma_buf[s_dma_pos], (uint16_t)(AT_UART_DMA_SIZE - s_dma_pos));
        wrote = (uint16_t)(wrote + rb_put(&s_rb, &s_dma_buf[0], pos));
    }

    uint16_t expected = (pos > s_dma_pos) ? (uint16_t)(pos - s_dma_pos)
                                          : (uint16_t)(AT_UART_DMA_SIZE - s_dma_pos + pos);
    if (wrote < expected) {
        s_stats.ring_dropped += (uint32_t)(expected - wrote);
    }

    s_dma_pos = pos;
    s_stats.rx_bytes += wrote;

    event_callback_t cb = s_rx_cb;
    if (cb != NULL) {
        (void)cb();
    }
}

/* Strong override of the weak vector in bsp/vector_table.c. */
void USART6_IRQHandler(void)
{
    uint32_t sr = MODEM_UART->SR;

    if ((sr & (USART_SR_IDLE | USART_SR_ORE)) != 0u) {
        if ((sr & USART_SR_IDLE) != 0u) {
            s_stats.idle_events++;
        }
        if ((sr & USART_SR_ORE) != 0u) {
            s_stats.overrun_errors++;
        }
        /* Documented clear sequence for IDLE/ORE: read SR, then read DR. */
        (void)MODEM_UART->SR;
        (void)MODEM_UART->DR;
        drain_dma();
    }

}

void DMA2_Stream1_IRQHandler(void)
{
    uint32_t flags = DMA2->LISR;
    DMA2->LIFCR = DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 |
                  DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1;
    if ((flags & DMA_LISR_TCIF1) != 0u) {
        s_stats.dma_wraps++;
    }
    if ((flags & (DMA_LISR_HTIF1 | DMA_LISR_TCIF1)) ==
        (DMA_LISR_HTIF1 | DMA_LISR_TCIF1)) {
        s_stats.dma_late_events++; /* service latency exceeded half a buffer */
    }
    if ((flags & (DMA_LISR_HTIF1 | DMA_LISR_TCIF1 | DMA_LISR_TEIF1 |
                  DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) != 0u) {
        drain_dma();
    }
    if ((flags & (DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1)) != 0u) {
        s_stats.dma_errors++;
        DMA2_Stream1->CR &= ~DMA_SxCR_EN;
        uint32_t guard = 10000u;
        while ((DMA2_Stream1->CR & DMA_SxCR_EN) != 0u && --guard != 0u) {
        }
        if (guard != 0u) {
            s_dma_pos = 0;
            DMA2_Stream1->M0AR = (uint32_t)(uintptr_t)s_dma_buf;
            DMA2_Stream1->NDTR = AT_UART_DMA_SIZE;
            DMA2_Stream1->CR |= DMA_SxCR_EN;
        }
    }
}

/* ------------------------------------------------- qosa_uart_* interface --- */

int qosa_uart_open(void)
{
    at_uart_init();
    return QOSA_OK;
}

int qosa_uart_write(int pos, const char *buffer, size_t size)
{
    (void)pos;
    if (buffer == NULL || size == 0u) {
        return 0;
    }

    size_t sent = 0;
    while (sent < size) {
        uint32_t guard = 2000000u;
        while (((MODEM_UART->SR & USART_SR_TXE) == 0u) && (--guard != 0u)) {
        }
        if (guard == 0u) {
            break; /* UART wedged; report a short write instead of hanging */
        }
        MODEM_UART->DR = (uint8_t)buffer[sent];
        sent++;
    }

    s_stats.tx_bytes += (uint32_t)sent;
    return (int)sent;
}

int qosa_uart_read(int pos, const char *buffer, size_t size)
{
    (void)pos;
    if (buffer == NULL || size == 0u) {
        return 0;
    }
    if (size > 0xFFFFu) {
        size = 0xFFFFu;
    }
    return (int)rb_get(&s_rb, (uint8_t *)buffer, (uint16_t)size);
}

void qosa_uart_register(event_callback_t event_cb)
{
    s_rx_cb = event_cb;
}

void at_uart_get_stats(at_uart_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->rx_bytes = s_stats.rx_bytes;
    out->tx_bytes = s_stats.tx_bytes;
    out->dma_wraps = s_stats.dma_wraps;
    out->idle_events = s_stats.idle_events;
    out->overrun_errors = s_stats.overrun_errors;
    out->ring_dropped = s_stats.ring_dropped;
    out->dma_errors = s_stats.dma_errors;
    out->dma_late_events = s_stats.dma_late_events;
}

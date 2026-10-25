#include "common.h"
#include <string.h>
#include "../../firmware/drivers/uart_at.c"
#include "../../firmware/drivers/ringbuffer.c"
static unsigned callback_count;
static int available(void) { ++callback_count; return 0; }

static void check_burst(unsigned size)
{
    rb_reset(&s_rb); s_dma_pos = 0; DMA2_Stream1->NDTR = AT_UART_DMA_SIZE;
    unsigned sent = 0, received = 0;
    while (sent < size) {
        unsigned pos = sent % AT_UART_DMA_SIZE;
        s_dma_buf[pos] = (uint8_t)(sent * 31u + 7u);
        ++sent;
        unsigned next = sent % AT_UART_DMA_SIZE;
        DMA2_Stream1->NDTR = next ? AT_UART_DMA_SIZE - next : AT_UART_DMA_SIZE;
        if (next == 256 || next == 0) {
            DMA2->LISR = next == 256 ? DMA_LISR_HTIF1 : DMA_LISR_TCIF1;
            DMA2_Stream1_IRQHandler();
            DMA2->LISR = 0;
            uint8_t bytes[300];
            unsigned n = rb_get(&s_rb, bytes, sizeof bytes);
            for (unsigned i = 0; i < n; ++i) CHECK(bytes[i] == (uint8_t)((received + i) * 31u + 7u));
            received += n;
        }
    }
    MODEM_UART->SR = USART_SR_IDLE;
    USART6_IRQHandler();
    uint8_t bytes[512];
    unsigned n = rb_get(&s_rb, bytes, sizeof bytes);
    for (unsigned i = 0; i < n; ++i) CHECK(bytes[i] == (uint8_t)((received + i) * 31u + 7u));
    received += n;
    CHECK(received == size);
    CHECK(rb_length(&s_rb) == 0);
}

void test_main(void)
{
    at_uart_init(); qosa_uart_register(available);
    CHECK((DMA2_Stream1->CR & (DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE)) ==
          (DMA_SxCR_HTIE | DMA_SxCR_TCIE | DMA_SxCR_TEIE));
    CHECK(NVIC_GetEnableIRQ(DMA2_Stream1_IRQn));
    CHECK(NVIC_GetPriority(DMA2_Stream1_IRQn) == NVIC_GetPriority(USART6_IRQn));
    const unsigned sizes[] = {1, 255, 256, 511, 512, 513, 1024, 2048, 8193, 65536};
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; ++i) check_burst(sizes[i]);
    unsigned before = s_stats.dma_errors;
    DMA2->LISR = DMA_LISR_TEIF1;
    DMA2_Stream1_IRQHandler();
    CHECK(s_stats.dma_errors == before + 1);
    CHECK(DMA2_Stream1->CR & DMA_SxCR_EN);
    CHECK(DMA2_Stream1->NDTR == AT_UART_DMA_SIZE);
    CHECK(callback_count > 0);
    test_finish(0);
}

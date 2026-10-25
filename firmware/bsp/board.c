/* Minimal board support: LEDs, console UART, printf retarget, fault reporting. */
#include "board.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "app_config.h"
#include "board_pins.h"

void Default_Handler(void)
{
    /* A weak vector that nobody overrode fired: report and park. */
    board_fault_report(0xDEAD0000u | (uint32_t)__get_IPSR());
    for (;;) {
    }
}

/* ------------------------------------------------------------------ LEDs -- */
void board_led_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN;
    (void)RCC->AHB1ENR;

    uint32_t moder = LED_GREEN_PORT->MODER;
    moder &= ~(3u << (LED_GREEN_PIN * 2u));
    moder |= (1u << (LED_GREEN_PIN * 2u));
    LED_GREEN_PORT->MODER = moder;

    moder = LED_BLUE_PORT->MODER;
    moder &= ~(3u << (LED_BLUE_PIN * 2u));
    moder |= (1u << (LED_BLUE_PIN * 2u));
    LED_BLUE_PORT->MODER = moder;

    moder = LED_RED_PORT->MODER;
    moder &= ~(3u << (LED_RED_PIN * 2u));
    moder |= (1u << (LED_RED_PIN * 2u));
    LED_RED_PORT->MODER = moder;
}

void board_led_set(board_led_t led, int on)
{
    GPIO_TypeDef *port = LED_GREEN_PORT;
    uint16_t pin = LED_GREEN_PIN;

    if (led == BOARD_LED_BLUE) {
        port = LED_BLUE_PORT;
        pin = LED_BLUE_PIN;
    } else if (led == BOARD_LED_RED) {
        port = LED_RED_PORT;
        pin = LED_RED_PIN;
    }

    if (on) {
        port->BSRR = BOARD_PIN(pin);
    } else {
        port->BSRR = (uint32_t)BOARD_PIN(pin) << 16u;
    }
}

/* --------------------------------------------------------------- console -- */
static volatile int s_console_ready;
#define CONSOLE_RX_SIZE 1024u
static uint8_t s_console_rx[CONSOLE_RX_SIZE];
static volatile uint16_t s_console_rx_head, s_console_rx_tail;
static volatile uint32_t s_console_rx_drops;
static volatile uint8_t s_console_rx_loss;

void board_console_rx_start(void)
{
    NVIC_DisableIRQ(USART3_IRQn);
    s_console_rx_head = s_console_rx_tail = 0;
    s_console_rx_drops = 0;
    s_console_rx_loss = 0;
    (void)CONSOLE_UART->SR;
    (void)CONSOLE_UART->DR;
    NVIC_ClearPendingIRQ(USART3_IRQn);
    /* Pure register/ring-buffer ISR: keep it above the kernel's BASEPRI=5
     * mask so a model copy cannot overrun the UART's single receive slot.
     * Never call FreeRTOS APIs from this handler at this priority. */
    NVIC_SetPriority(USART3_IRQn, 4);
    CONSOLE_UART->CR1 |= USART_CR1_RXNEIE;
    NVIC_EnableIRQ(USART3_IRQn);
}

void USART3_IRQHandler(void)
{
    uint32_t status = CONSOLE_UART->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
        uint8_t data = (uint8_t)CONSOLE_UART->DR; /* SR then DR clears error flags. */
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE)) {
            ++s_console_rx_drops;
            s_console_rx_loss = 1;
        }
        if ((status & USART_SR_RXNE) && !(status & (USART_SR_FE | USART_SR_NE | USART_SR_PE))) {
            uint16_t head = s_console_rx_head;
            if ((uint16_t)(head - s_console_rx_tail) == CONSOLE_RX_SIZE) {
                ++s_console_rx_drops;
                s_console_rx_loss = 1;
            }
            else {
                s_console_rx[head & (CONSOLE_RX_SIZE-1u)] = data;
                __DMB();
                s_console_rx_head = head + 1u;
            }
        }
    }
}

int board_console_getc(void)
{
    /* The loss flag and the byte must form one read transaction. Otherwise an
     * RX overrun between the flag check and a received newline could complete
     * a damaged command before the next read reports the loss. Keep this lock
     * to the few memory accesses below, and preserve the caller's IRQ state. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    if (s_console_rx_loss) {
        /* Keep the loss marker aligned with the command stream. Flush bytes
         * around the gap so no truncated command can be interpreted as valid. */
        s_console_rx_tail = s_console_rx_head;
        s_console_rx_loss = 0;
        __set_PRIMASK(primask);
        return -2;
    }
    uint16_t tail = s_console_rx_tail;
    if (tail == s_console_rx_head) {
        __set_PRIMASK(primask);
        return -1;
    }
    __DMB();
    int value = s_console_rx[tail & (CONSOLE_RX_SIZE-1u)];
    __DMB();
    s_console_rx_tail = tail + 1u;
    __set_PRIMASK(primask);
    return value;
}

uint32_t board_console_rx_drops(void) { return s_console_rx_drops; }

void board_console_init(uint32_t baud)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;
    (void)RCC->APB1ENR;

    /* PD8/PD9 alternate function 7. */
    uint32_t moder = CONSOLE_TX_PORT->MODER;
    moder &= ~((3u << (CONSOLE_TX_PIN * 2u)) | (3u << (CONSOLE_RX_PIN * 2u)));
    moder |= ((2u << (CONSOLE_TX_PIN * 2u)) | (2u << (CONSOLE_RX_PIN * 2u)));
    CONSOLE_TX_PORT->MODER = moder;

    CONSOLE_TX_PORT->AFR[1] &= ~((0xFu << ((CONSOLE_TX_PIN - 8u) * 4u)) |
                                 (0xFu << ((CONSOLE_RX_PIN - 8u) * 4u)));
    CONSOLE_TX_PORT->AFR[1] |= ((7u << ((CONSOLE_TX_PIN - 8u) * 4u)) |
                                (7u << ((CONSOLE_RX_PIN - 8u) * 4u)));

    CONSOLE_UART->CR1 = 0;
    CONSOLE_UART->BRR = (SystemCoreClock / 2u + baud / 2u) / baud; /* APB1 = 50 MHz */
    CONSOLE_UART->CR2 = 0;
    CONSOLE_UART->CR3 = 0;
    CONSOLE_UART->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;

    s_console_ready = 1;
}

void board_console_putc(char c)
{
    if (!s_console_ready) {
        return;
    }
    uint32_t guard = 2000000u;
    while ((CONSOLE_UART->SR & USART_SR_TXE) == 0u && --guard != 0u) { }
    if (guard == 0u) return;
    CONSOLE_UART->DR = (uint8_t)c;
}

void board_console_write(const char *s, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (s[i] == '\n') {
            board_console_putc('\r');
        }
        board_console_putc(s[i]);
    }
}

__attribute__((weak)) int board_log_enqueue(const char *line)
{
    (void)line;
    return 0;
}

void board_log(const char *fmt, ...)
{
    char buf[192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        if (!board_log_enqueue(buf)) {
            board_console_write(buf, (size_t)((n < (int)sizeof buf) ? n : (int)sizeof buf - 1));
            board_console_write("\n", 1);
        }
    }
}

/* newlib retarget: printf/puts go to the ST-LINK virtual COM port. */
int _write(int fd, char *ptr, int len)
{
    (void)fd;
    board_console_write(ptr, (size_t)len);
    return len;
}

int _read(int fd, char *ptr, int len)
{
    (void)fd;
    (void)ptr;
    (void)len;
    errno = EAGAIN;
    return -1;
}

int _close(int fd) { (void)fd; return -1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
void _exit(int code) { (void)code; for (;;) { } }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }

/* Minimal sbrk over the linker-provided heap area. */
extern char end;   /* set by _user_heap_stack in the linker script */
extern char _estack;
extern char __libc_heap_limit;

void *_sbrk(ptrdiff_t incr)
{
    static uintptr_t heap_end;
    uintptr_t start = (uintptr_t)&end;
    uintptr_t limit = (uintptr_t)&__libc_heap_limit;
    uintptr_t msp = __get_MSP();
    if (msp < limit) limit = msp;
    if (heap_end == 0u) heap_end = start;
    uintptr_t prev = heap_end;
    if (incr >= 0) {
        if (heap_end > limit || (uintptr_t)incr > limit - heap_end) {
            errno = ENOMEM;
            return (void *)-1;
        }
        heap_end += (uintptr_t)incr;
    } else {
        uintptr_t decrease = (uintptr_t)(-(incr + 1)) + 1u;
        if (decrease > heap_end - start) {
            errno = EINVAL;
            return (void *)-1;
        }
        heap_end -= decrease;
    }
    return (void *)prev;
}

/* ----------------------------------------------------------- fault report -- */
static volatile uint32_t s_fault_pc;
static volatile uint32_t s_fault_lr;
static volatile uint32_t s_fault_psr;
static volatile uint32_t s_fault_exc_return;
static volatile uint32_t s_fault_cfsr, s_fault_hfsr, s_fault_bfar, s_fault_mmfar;

/* Fatal paths bypass newlib: a fault can interrupt a held libc lock. */
static void fault_hex(const char *name, uint32_t value)
{
    board_console_write(name, strlen(name));
    for (int shift = 28; shift >= 0; shift -= 4)
        board_console_putc("0123456789ABCDEF"[(value >> shift) & 15u]);
    board_console_write("\n", 1);
}

void board_fault_report(uint32_t code)
{
    __disable_irq();
    board_console_write("\n*** FAULT ***\n", 15);
    fault_hex("code=", code);
    fault_hex("pc=", s_fault_pc);
    fault_hex("lr=", s_fault_lr);
    fault_hex("xpsr=", s_fault_psr);
    fault_hex("exc_return=", s_fault_exc_return);
    fault_hex("cfsr=", s_fault_cfsr);
    fault_hex("hfsr=", s_fault_hfsr);
    fault_hex("bfar=", s_fault_bfar);
    fault_hex("mmfar=", s_fault_mmfar);
    for (;;) {
        board_led_set(BOARD_LED_RED, 1);
    }
}

__attribute__((noreturn))
void board_hardfault_c(uint32_t *raw_frame, uint32_t exc_return)
{
    s_fault_exc_return = exc_return;
    s_fault_cfsr = SCB->CFSR;
    s_fault_hfsr = SCB->HFSR;
    s_fault_bfar = SCB->BFAR;
    s_fault_mmfar = SCB->MMFAR;
    uintptr_t frame = (uintptr_t)raw_frame;
    if ((exc_return & (1u << 4)) == 0u) frame += 18u * sizeof(uint32_t);
    if ((frame & 3u) == 0u && frame >= 0x20000000u &&
        frame <= 0x20050000u - 8u * sizeof(uint32_t) &&
        (s_fault_cfsr & ((1u << 4) | (1u << 12))) == 0u) {
        uint32_t *stacked = (uint32_t *)frame;
        s_fault_pc = stacked[6];
        s_fault_lr = stacked[5];
        s_fault_psr = stacked[7];
    }
    board_fault_report(0xDEAD0001u);
    __builtin_unreachable();
}

__attribute__((naked)) void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "mov r1, lr\n"
        "b board_hardfault_c\n");
}

/* ------------------------------------------------------------- board init -- */
void board_init(void)
{
    SystemCoreClockUpdate();

    /* QADP V1.1 D9/PD15 -> Q206 -> EC200U RESET_N. A high MCU output holds the
     * modem in reset. Preload low before selecting push-pull output, and keep
     * this pin owned here rather than by the LCD driver. */
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIODEN;
    (void)RCC->AHB1ENR;
    MODEM_RESET_PORT->BSRR = (uint32_t)BOARD_PIN(MODEM_RESET_PIN) << 16u;
    MODEM_RESET_PORT->OTYPER &= ~BOARD_PIN(MODEM_RESET_PIN);
    MODEM_RESET_PORT->PUPDR &= ~(3u << (MODEM_RESET_PIN * 2u));
    MODEM_RESET_PORT->MODER =
        (MODEM_RESET_PORT->MODER & ~(3u << (MODEM_RESET_PIN * 2u))) |
        (1u << (MODEM_RESET_PIN * 2u));

    board_led_init();
    board_console_init(CONSOLE_BAUDRATE);

    board_console_write("\n", 1);
    board_log("UniKnect Gen1 Pro C  fw=%s  build=%s %s", APP_FW_VERSION, __DATE__, __TIME__);
    board_log("core: STM32F413ZH  sysclk=%lu Hz  hse=%s",
              (unsigned long)SystemCoreClock,
              ((RCC->CR & RCC_CR_HSERDY) != 0u) ? "ready" : "fallback to HSI");
    board_log("sram: sram1=256K sram2=64K  console=USART3 115200 8N1");
}

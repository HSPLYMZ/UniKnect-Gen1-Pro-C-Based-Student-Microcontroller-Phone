/* System clock for STM32F413, target selected by CFG_CPU_MHZ.
 *
 * HSE is 8 MHz from the ST-LINK MCO on a NUCLEO-144 board. When the board is
 * powered only from the QADP 5 V rail (S201 on "ARDU"), the ST-LINK may be
 * unpowered and HSE never starts, so we fall back to HSI 16 MHz and keep the
 * same target. This mirrors the field condition of the previous
 * MicroPython project, which also ran with the extension board supplying power.
 */
#include "stm32f4xx.h"
#include "app_config.h"
_Static_assert(CFG_CPU_MHZ>=50u && CFG_CPU_MHZ<=100u,"Unsupported PLL target");

#if !defined(HSE_VALUE)
#define HSE_VALUE 8000000u
#endif
#if !defined(HSI_VALUE)
#define HSI_VALUE 16000000u
#endif

uint32_t SystemCoreClock = 16000000u;
const uint8_t AHBPrescTable[16] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3, 4, 6, 7, 8, 9};
const uint8_t APBPrescTable[8] = {0, 0, 0, 0, 1, 2, 3, 4};

static void clock_pll_from(uint32_t pll_m, uint32_t pll_src);

static int hse_start(void)
{
    uint32_t guard = 0x20000u;

    RCC->CR |= RCC_CR_HSEON;
    while ((RCC->CR & RCC_CR_HSERDY) == 0u) {
        if (--guard == 0u) {
            RCC->CR &= ~RCC_CR_HSEON;
            return -1;
        }
    }
    return 0;
}

void SystemInit(void)
{
    /* FPU: CP10/CP11 full access, then enable. */
    SCB->CPACR |= (0xFu << 20);
    __DSB();
    __ISB();

    /* Reset RCC to a known state, then run from HSI while we retune the PLL. */
    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {
    }
    RCC->CFGR = RCC_CFGR_PPRE1_DIV2;
    RCC->CR &= ~(RCC_CR_PLLON | RCC_CR_CSSON | RCC_CR_HSEBYP);
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
    }
    RCC->PLLCFGR = 0x24003010u; /* reset value */

    if (hse_start() == 0) {
        clock_pll_from(8u, RCC_PLLCFGR_PLLSRC_HSE);
    } else {
        clock_pll_from(16u, RCC_PLLCFGR_PLLSRC_HSI);
    }

    /* Vector table lives at the start of flash. */
    SCB->VTOR = FLASH_BASE;
}

/* VCO=2*target, SYSCLK=VCO/2; APB1=/2 and APB2=/1. */
static void clock_pll_from(uint32_t pll_m, uint32_t pll_src)
{
    uint32_t guard = 0x20000u;

    FLASH->ACR = FLASH_ACR_LATENCY_3WS | FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN;

    RCC->CFGR = RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV2 | RCC_CFGR_PPRE2_DIV1;

    RCC->PLLCFGR = (pll_m & 0x3Fu)
                 | ((CFG_CPU_MHZ*2u) << 6)
                 | (((2u >> 1u) - 1u) << 16)
                 | pll_src
                 | (8u << 24);

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
        if (--guard == 0u) {
            return; /* stay on HSI 16 MHz; the console still works */
        }
    }

    RCC->CFGR |= RCC_CFGR_SW_PLL;
    guard = 0x20000u;
    while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_PLL) {
        if (--guard == 0u) {
            RCC->CFGR &= ~RCC_CFGR_SW;
            guard = 0x20000u;
            while ((RCC->CFGR & RCC_CFGR_SWS) != RCC_CFGR_SWS_HSI && --guard != 0u) { }
            if (guard == 0u) {
                __disable_irq(); /* no stable source: fail before task startup */
                for (;;) { __NOP(); }
            }
            return;
        }
    }

    SystemCoreClock = CFG_CPU_MHZ*1000000u;
}

void SystemCoreClockUpdate(void)
{
    uint32_t cfgr = RCC->CFGR;
    uint32_t source = cfgr & RCC_CFGR_SWS;
    uint32_t clock = HSI_VALUE;
    if (source == RCC_CFGR_SWS_HSE) clock = HSE_VALUE;
    else if (source == RCC_CFGR_SWS_PLL) {
        uint32_t pll = RCC->PLLCFGR;
        uint32_t m = pll & RCC_PLLCFGR_PLLM;
        uint32_t n = (pll & RCC_PLLCFGR_PLLN) >> RCC_PLLCFGR_PLLN_Pos;
        uint32_t p = (((pll & RCC_PLLCFGR_PLLP) >> RCC_PLLCFGR_PLLP_Pos) + 1u) * 2u;
        uint32_t input = (pll & RCC_PLLCFGR_PLLSRC) ? HSE_VALUE : HSI_VALUE;
        if (m != 0u) clock = (input / m) * n / p;
    }
    SystemCoreClock = clock >> AHBPrescTable[(cfgr & RCC_CFGR_HPRE) >> RCC_CFGR_HPRE_Pos];
}

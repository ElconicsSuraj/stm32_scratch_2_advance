#include "dwt_delay.h"

void DWT_Delay_Init(void)
{
    // Enable trace and debug blocks (TRC)
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    // Unlock DWT (if LAR exists on your MCU)
#ifdef DWT_LAR_KEY
    DWT->LAR = 0xC5ACCE55; // Unlock DWT (needed for some MCUs)
#endif

    // Reset the cycle counter
    DWT->CYCCNT = 0;

    // Enable the cycle counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

//void delay_us(uint32_t us)
//{
//    uint32_t startTick = DWT->CYCCNT;
//    uint32_t ticks = us * (SystemCoreClock / 1000000U);
//    while ((DWT->CYCCNT - startTick) < ticks);
//}

void delay_ms(uint32_t ms)
{
    while (ms--) delay_us(1000);
}

#ifndef INC_DWT_DELAY_H_
#define INC_DWT_DELAY_H_

#include "stm32wlxx_hal.h"

void DWT_Delay_Init(void);
void delay_us(uint32_t us);
void delay_ms(uint32_t ms);

#endif /* INC_DWT_DELAY_H_ */

/*
 * ds18b20.h
 *
 *  Created on: Oct 30, 2025
 *      Author: Admin
 */

#ifndef INC_DS18B20_H_
#define INC_DS18B20_H_


#include "main.h"

float DS18B20_ReadTemp(void);
void DS18B20_Init(TIM_HandleTypeDef *htim, GPIO_TypeDef *port, uint16_t pin);





#endif /* INC_DS18B20_H_ */

/*
 * ds18b20.c
 *
 *  Created on: Oct 30, 2025
 *      Author: Admin
 */


#include "ds18b20.h"

static TIM_HandleTypeDef *timer;
static GPIO_TypeDef *data_port;
static uint16_t data_pin;

void delay_us(uint16_t us)
{
    __HAL_TIM_SET_COUNTER(timer, 0);
    while (__HAL_TIM_GET_COUNTER(timer) < us);
}

void DS18B20_Init(TIM_HandleTypeDef *htim, GPIO_TypeDef *port, uint16_t pin)
{
    timer = htim;
    data_port = port;
    data_pin = pin;
    HAL_TIM_Base_Start(timer);
}

static void set_pin_output(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = data_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(data_port, &GPIO_InitStruct);
}

static void set_pin_input(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = data_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(data_port, &GPIO_InitStruct);
}

static uint8_t reset_pulse(void)
{
    uint8_t presence = 0;

    set_pin_output();
    HAL_GPIO_WritePin(data_port, data_pin, GPIO_PIN_RESET);
    delay_us(480);

    set_pin_input();
    delay_us(70);

    presence = !HAL_GPIO_ReadPin(data_port, data_pin);
    delay_us(410);

    return presence;
}

static void write_bit(uint8_t bit)
{
    set_pin_output();
    HAL_GPIO_WritePin(data_port, data_pin, GPIO_PIN_RESET);
    delay_us(2);

    if (bit)
        set_pin_input();

    delay_us(60);
    set_pin_input();
}

static uint8_t read_bit(void)
{
    uint8_t bit = 0;

    set_pin_output();
    HAL_GPIO_WritePin(data_port, data_pin, GPIO_PIN_RESET);
    delay_us(2);
    set_pin_input();
    delay_us(10);

    if (HAL_GPIO_ReadPin(data_port, data_pin))
        bit = 1;

    delay_us(50);
    return bit;
}

static void write_byte(uint8_t data)
{
    for (uint8_t i = 0; i < 8; i++)
    {
        write_bit(data & 0x01);
        data >>= 1;
    }
}

static uint8_t read_byte(void)
{
    uint8_t value = 0;

    for (uint8_t i = 0; i < 8; i++)
    {
        value |= (read_bit() << i);
    }

    return value;
}

float DS18B20_ReadTemp(void)
{
    uint8_t presence = reset_pulse();
    if (!presence)
        return -127.0f;

    write_byte(0xCC); // Skip ROM
    write_byte(0x44); // Convert T
    HAL_Delay(800);   // Wait for conversion

    presence = reset_pulse();
    write_byte(0xCC);
    write_byte(0xBE);

    uint8_t temp_LSB = read_byte();
    uint8_t temp_MSB = read_byte();

    int16_t rawTemp = (temp_MSB << 8) | temp_LSB;
    float temperature = (float)rawTemp / 16.0f;

    return temperature;
}

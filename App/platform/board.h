/**
 * board.h - FK407M1-V1.1 (STM32F407VET6) board pin map.
 *
 * Single source of truth for board-specific pins. Confirmed from the FK407M1
 * hardware reference (stm32-base.org):
 *   - User LED:  PC13, ACTIVE-LOW (LED on when the pin is driven LOW)
 *   - User KEY:  PA15, active-low
 *   - HSE xtal:  8 MHz  (PLL -> 168 MHz SYSCLK)
 *   - SWD:       PA13 (SWDIO), PA14 (SWCLK)
 */
#pragma once

#include "main.h" /* CubeMX-generated: pulls in stm32f4xx_hal.h + any user-labelled pins */
#include "gpio.hpp"

/* If the CubeMX "LED" user label is set on PC13, main.h already defines these.
 * Fall back to the known board pin so this builds even without the label. */
#ifndef LED_GPIO_Port
#define LED_GPIO_Port GPIOC
#endif
#ifndef LED_Pin
#define LED_Pin GPIO_PIN_13
#endif

/* Active-low user LED. */
inline const gpio::OutputPin led{LED_GPIO_Port, LED_Pin, /*active_high=*/false};

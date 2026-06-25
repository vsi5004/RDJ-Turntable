/**
 * board_temp.h - STM32F407 internal core-temperature readout via ADC1 + DMA.
 *
 * ADC1 runs a 3-rank scan into a circular DMA buffer [arm-angle(IN10), temp-sensor, Vrefint].
 * This module owns starting that DMA and converting the temp/Vrefint words to whole degrees C.
 * The F407 temp sensor has NO factory calibration, so it uses the datasheet typicals (V25 = 0.76 V,
 * slope = 2.5 mV/degC) - rough (a "is the board hot" indicator), refined only by Vrefint for VDDA.
 */
#pragma once
#include <cstdint>

namespace board_temp {

/* Returned by celsius() until the first valid conversion (matches hmi::kTempUnavailable). */
constexpr int16_t kUnavailable = INT16_MIN;

/* Start ADC1 in continuous-scan circular DMA. Call once at boot after the HAL/ADC is up. */
void init();

/* Latest board temperature in whole degrees C, or kUnavailable before the first conversion. */
int16_t celsius();

}  // namespace board_temp

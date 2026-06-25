/**
 * board_temp.cpp - read ADC1's circular DMA buffer and convert to core temperature.
 *
 * Buffer layout matches the ADC1 rank order set in CubeMX (docs/M5-cubemx-setup.md):
 *   [0] = IN10 / PC0  (tonearm angle - not used here)
 *   [1] = internal temperature sensor (ADC_CHANNEL_TEMPSENSOR)
 *   [2] = Vrefint     (ADC_CHANNEL_VREFINT)
 *
 * The raw sensor jitters a couple of LSB, which would flip the displayed integer degree every
 * frame, so the raw counts are run through a time-throttled exponential moving average and the
 * reported whole-degree value carries a small deadband - the readout only moves on a real change.
 */
#include "board_temp.h"

#include "adc.h"  /* CubeMX-generated: extern ADC_HandleTypeDef hadc1; pulls in main.h (HAL_GetTick) */

namespace board_temp {
namespace {

/* DMA target. uint16_t per the half-word DMA config; hardware-written, so volatile. */
volatile uint16_t dma_buf[3] = {0, 0, 0};

/* F407 factory VREFINT calibration: raw 12-bit ADC reading of Vrefint at VDDA = 3.3 V, ~30 C. */
const uint16_t* const kVrefintCal = reinterpret_cast<const uint16_t*>(0x1FFF7A2AUL);

/* Temp-sensor datasheet typicals (no factory cal on F407). degC*10 = (Vsense_mV - 760) * 4 + 250. */
constexpr int32_t kV25Millivolts = 760;       /* sensor output at 25 C */
constexpr int32_t kVddaCalMillivolts = 3300;  /* VDDA at which kVrefintCal was taken */
constexpr int32_t kAdcFullScale = 4095;       /* 12-bit */

/* Smoothing: fold one raw sample into the EMA every kSampleIntervalMs; alpha = 1 / 2^kEmaShift.
 * Time constant ~= 2^kEmaShift * interval = 32 * 50 ms ~= 1.6 s. */
constexpr uint32_t kSampleIntervalMs = 50;
constexpr int kEmaShift = 5;
constexpr int32_t kDeadbandTenths = 7;        /* report only moves on a >0.7 C real change */

uint32_t temp_acc = 0;   /* raw temp count << kEmaShift */
uint32_t vref_acc = 0;   /* raw vref count << kEmaShift */
bool ema_primed = false;
uint32_t last_sample_ms = 0;
int16_t reported_c = kUnavailable;

void fold_sample()
{
    const uint16_t vref_raw = dma_buf[2];
    const uint16_t temp_raw = dma_buf[1];
    if (vref_raw == 0) return;  /* DMA not yet primed */
    if (!ema_primed) {
        temp_acc = static_cast<uint32_t>(temp_raw) << kEmaShift;
        vref_acc = static_cast<uint32_t>(vref_raw) << kEmaShift;
        ema_primed = true;
        return;
    }
    temp_acc += temp_raw - (temp_acc >> kEmaShift);
    vref_acc += vref_raw - (vref_acc >> kEmaShift);
}

/* Smoothed counts -> temperature in tenths of a degree C. */
int32_t smoothed_tenths()
{
    const int32_t vref_cal = *kVrefintCal;
    const int32_t vref_avg = static_cast<int32_t>(vref_acc >> kEmaShift);
    const int32_t temp_avg = static_cast<int32_t>(temp_acc >> kEmaShift);
    const int32_t vdda_mv = kVddaCalMillivolts * vref_cal / vref_avg;
    const int32_t vsense_mv = temp_avg * vdda_mv / kAdcFullScale;
    return (vsense_mv - kV25Millivolts) * 4 + 250;
}

}  // namespace

void init()
{
    HAL_ADC_Start_DMA(&hadc1,
                      reinterpret_cast<uint32_t*>(const_cast<uint16_t*>(dma_buf)),
                      3);
}

int16_t celsius()
{
    const uint32_t now = HAL_GetTick();
    if (!ema_primed || static_cast<uint32_t>(now - last_sample_ms) >= kSampleIntervalMs) {
        last_sample_ms = now;
        fold_sample();
    }
    if (!ema_primed) return kUnavailable;

    const int32_t tenths = smoothed_tenths();
    const int32_t rounded = (tenths >= 0 ? tenths + 5 : tenths - 5) / 10;
    if (reported_c == kUnavailable
        || tenths - static_cast<int32_t>(reported_c) * 10 > kDeadbandTenths
        || tenths - static_cast<int32_t>(reported_c) * 10 < -kDeadbandTenths) {
        reported_c = static_cast<int16_t>(rounded);
    }
    return reported_c;
}

}  // namespace board_temp

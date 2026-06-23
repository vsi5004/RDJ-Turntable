#include "system_adapters.hpp"

#include "control/foc.h"
#include "main.h"
#include "trace.h"

namespace platform {
namespace {

constexpr float kTwoPi = 6.28318530717959f;
constexpr float kBeltRatio = 90.0f / 24.0f;

float motor_velocity(turntable::RecordSpeed speed)
{
    const float platter_rpm = speed == turntable::RecordSpeed::Rpm45 ? 45.0f : 33.3333f;
    return platter_rpm * kBeltRatio * (kTwoPi / 60.0f);
}

}  // namespace

uint32_t HalClock::now_ms() const { return HAL_GetTick(); }

void BenchPlatter::start(turntable::RecordSpeed speed, uint32_t)
{
    if (!foc::alignment_valid) {
        TRACE("platter start rejected: alignment is not valid\n");
        foc::stop();
        return;
    }
    foc::set_closed_loop_velocity(motor_velocity(speed));
}

void BenchPlatter::stop(uint32_t) { foc::stop(); }
void BenchPlatter::emergency_stop() { foc::stop(); }

void UnavailableCarriage::home(uint32_t) { TRACE("carriage unavailable: home ignored\n"); }
void UnavailableCarriage::establish_home_reference() {}
void UnavailableCarriage::move_to_park(uint32_t) { TRACE("carriage unavailable: park ignored\n"); }
void UnavailableCarriage::move_to_lead_in(uint32_t)
{
    TRACE("carriage unavailable: lead-in ignored\n");
}
void UnavailableCarriage::stop() {}
void UnavailableCarriage::set_tracking(bool) {}
void UnavailableCarriage::mark_home_valid() {}
void UnavailableCarriage::invalidate_home() {}
turntable::HomeConfidence UnavailableCarriage::home_confidence() const
{
    return turntable::HomeConfidence::Unknown;
}
bool UnavailableCarriage::at_park() const { return false; }

void UnavailableLift::raise(uint32_t) { TRACE("tonearm lift unavailable: raise ignored\n"); }
void UnavailableLift::lower(uint32_t) { TRACE("tonearm lift unavailable: lower ignored\n"); }

}  // namespace platform

/**
 * app.cpp - application composition root and cooperative main-loop tick.
 *
 * Until the full mechanics are assembled the firmware boots into the diagnostic authority. The
 * proven M2c platter exercises are now typed diagnostic commands rather than independent booleans
 * and duplicate key polling. Normal turntable behavior is present behind unavailable-safe adapters.
 */
#include "app.h"

#include "board.h"
#include "control/as5048a.h"
#include "control/foc.h"
#include "control/mt6826s.h"
#include "diagnostics/diagnostic_controller.hpp"
#include "diagnostics/m2c_executor.hpp"
#include "hmi/backlight_controller.hpp"
#include "hmi/interaction_controller.hpp"
#include "hmi/screenkey_demo.hpp"
#include "hmi/screens.h"
#include "nvm.h"
#include "platform/debounced_button.hpp"
#include "platform/system_adapters.hpp"
#include "trace.h"
#include "turntable/application_controller.hpp"

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kOpenLoopElecVel = 2.0f * kPi;
constexpr float kBeltRatio = 90.0f / 24.0f;
constexpr float kMotorVelocity33 = 33.3333f * kBeltRatio * (2.0f * kPi / 60.0f);
constexpr uint32_t kGlobalStopHoldMs = 800;
constexpr uint32_t kCalibrationHoldMs = 2500;

hmi::BacklightController backlight_controller;

#if RDJ_SCREENKEY_DEMO
hmi::ScreenKeyDemo screenkey_demo({kOpenLoopElecVel, kMotorVelocity33});
#else
platform::HalClock clock_source;
platform::BenchPlatter platter;
platform::UnavailableCarriage carriage;
platform::UnavailableLift lift;
turntable::Controller turntable_controller(clock_source, platter, carriage, lift);
diagnostics::M2cExecutor m2c_executor(clock_source);
diagnostics::Controller diagnostic_controller(m2c_executor);
turntable::ApplicationController application(turntable_controller, diagnostic_controller);
hmi::InteractionController key_interaction({kOpenLoopElecVel, kMotorVelocity33});
#endif

gpio::DebouncedButton key0{gpio::InputPin{KEY0_GPIO_Port, KEY0_Pin, false}};
gpio::DebouncedButton key1{gpio::InputPin{KEY1_GPIO_Port, KEY1_Pin, false}};
gpio::DebouncedButton key2{gpio::InputPin{KEY2_GPIO_Port, KEY2_Pin, false}};

hmi::Gesture gesture(gpio::ButtonEvent event)
{
    if (event == gpio::ButtonEvent::Tap) return hmi::Gesture::Tap;
    if (event == gpio::ButtonEvent::Held) return hmi::Gesture::Hold;
    return hmi::Gesture::None;
}

void note_activity(gpio::ButtonEvent event, uint32_t now)
{
    if (event != gpio::ButtonEvent::None) backlight_controller.note_activity(now);
}

#if RDJ_SCREENKEY_DEMO
void handle_demo_keys(uint32_t now)
{
    const gpio::ButtonEvent transport = key0.update(now, kGlobalStopHoldMs);
    const gpio::ButtonEvent speed = key1.update(now, kCalibrationHoldMs);
    const gpio::ButtonEvent settings = key2.update(now, kGlobalStopHoldMs);
    note_activity(transport, now);
    note_activity(speed, now);
    note_activity(settings, now);
    screenkey_demo.handle(hmi::Key::Transport, gesture(transport), now);
    screenkey_demo.handle(hmi::Key::Speed, gesture(speed), now);
    screenkey_demo.handle(hmi::Key::Settings, gesture(settings), now);
}
#else
void apply(const hmi::Intent& intent)
{
    switch (intent.type) {
    case hmi::IntentType::None:
        break;
    case hmi::IntentType::TurntableEvent:
        application.post(intent.event);
        break;
    case hmi::IntentType::EnterDiagnostics:
        application.request_diagnostic_entry();
        break;
    case hmi::IntentType::ExitDiagnostics:
        application.request_diagnostic_exit();
        break;
    case hmi::IntentType::SubmitDiagnostic:
        application.submit_diagnostic(intent.diagnostic);
        break;
    case hmi::IntentType::AbortDiagnostic:
        application.abort_diagnostic();
        break;
    case hmi::IntentType::AcknowledgeDiagnosticFault:
        application.acknowledge_diagnostic_fault();
        break;
    }
}

void handle_keys(uint32_t now)
{
    const gpio::ButtonEvent transport = key0.update(now, kGlobalStopHoldMs);
    const gpio::ButtonEvent speed = key1.update(now, kCalibrationHoldMs);
    const gpio::ButtonEvent settings = key2.update(now, kGlobalStopHoldMs);
    note_activity(transport, now);
    note_activity(speed, now);
    note_activity(settings, now);

    turntable::ApplicationSnapshot snapshot = application.snapshot();
    apply(key_interaction.handle(hmi::Key::Transport, gesture(transport), snapshot));
    snapshot = application.snapshot();
    apply(key_interaction.handle(hmi::Key::Speed, gesture(speed), snapshot));
    snapshot = application.snapshot();
    apply(key_interaction.handle(hmi::Key::Settings, gesture(settings), snapshot));
}

void trace_status(uint32_t now)
{
    static uint32_t last_trace = 0;
    if (static_cast<uint32_t>(now - last_trace) < 200) return;
    last_trace = now;

    const diagnostics::Snapshot diagnostic = diagnostic_controller.snapshot();
    if (diagnostic.state == diagnostics::State::Running
        && diagnostic.command.action == diagnostics::Action::ClosedLoopVelocity) {
        const int32_t vel_x10 = static_cast<int32_t>(diagnostic.report.primary * 10.0f);
        const int32_t uq_mv = static_cast<int32_t>(diagnostic.report.secondary * 1000.0f);
        TRACE("DIAG LOOP vel=%ld.%ld rad/s Uq=%ldmV FAULT=%u\n",
              static_cast<long>(vel_x10 / 10),
              static_cast<long>(vel_x10 < 0 ? -vel_x10 % 10 : vel_x10 % 10),
              static_cast<long>(uq_mv), static_cast<unsigned>(foc::faulted()));
        return;
    }

    if (diagnostic.state == diagnostics::State::Running
        && diagnostic.command.action == diagnostics::Action::OpenLoopSpin) {
        mt6826s::Reading reading;
        mt6826s::read(reading);
        const uint32_t mech_x10 = static_cast<uint32_t>(reading.angle) * 3600u / 32768u;
        TRACE("DIAG SPIN mech=%u.%u deg crc=%s FAULT=%u\n",
              static_cast<unsigned>(mech_x10 / 10), static_cast<unsigned>(mech_x10 % 10),
              reading.crc_ok ? "OK" : "BAD", static_cast<unsigned>(foc::faulted()));
        return;
    }

    if (diagnostic.state == diagnostics::State::Ready) {
        mt6826s::Reading platter_angle;
        mt6826s::read(platter_angle);
        as5048a::Reading arm_angle;
        as5048a::read(arm_angle);
        TRACE("DIAG READY platter=%u crc=%s arm=%u parity=%s\n",
              platter_angle.angle, platter_angle.crc_ok ? "OK" : "BAD", arm_angle.angle,
              arm_angle.parity_ok ? "OK" : "BAD");
    }
}
#endif

}  // namespace

void app_init(void)
{
    trace_init();
    led.off();
    TRACE("\n=== RDJ-Turntable application ===\n");
    TRACE("SYSCLK = %u Hz\n", static_cast<unsigned>(HAL_RCC_GetSysClockFreq()));

    screens::init();
    backlight_controller.reset(HAL_GetTick());
#if RDJ_SCREENKEY_DEMO
    /* GPIO initialization already holds PLAT_EN low. Reassert it and deliberately do not start
     * TIM1 PWM, load motor calibration, or construct any actuator command path. */
    foc::disable();
    screenkey_demo.reset(HAL_GetTick());
    screens::show(hmi::present(screenkey_demo.application_snapshot(),
                               screenkey_demo.navigation_snapshot(), 0,
                               &screenkey_demo.speed_trace()));
    TRACE("ScreenKey demo: actuator control is disabled.\n");
    TRACE("KEY0 transport/hold stop, KEY1 speed/select, KEY2 settings/next.\n");
    TRACE("Hold KEY2 on the primary view to inject a demo fault.\n");
#else
    foc::init();

    nvm::Cal cal;
    if (nvm::load(cal)) {
        foc::zero_elec_offset = cal.zero_offset;
        foc::direction = static_cast<int8_t>(cal.direction);
        foc::alignment_valid = true;
        TRACE("Loaded platter alignment: dir=%c\n", cal.direction > 0 ? '+' : '-');
    } else {
        TRACE("No stored platter alignment; run KEY1 tap before closed-loop velocity.\n");
    }

#if RDJ_BOOT_DIAGNOSTICS
    application.boot_diagnostics();
#endif
    key_interaction.synchronize(application.snapshot());
    screens::show(hmi::present(application.snapshot(), key_interaction.snapshot()));
#if RDJ_BOOT_DIAGNOSTICS
    TRACE("Diagnostics ready: KEY0 spin, KEY1 align / hold auto-cal, KEY2 velocity.\n");
    TRACE("Hold KEY0 for global stop.\n");
#else
    TRACE("Normal control authority ready.\n");
#endif
#endif
}

void app_run(void)
{
    static uint32_t last_blink = 0;
    const uint32_t now = HAL_GetTick();

#if RDJ_SCREENKEY_DEMO
    handle_demo_keys(now);
    screenkey_demo.tick(now);
    screens::show(hmi::present(screenkey_demo.application_snapshot(),
                               screenkey_demo.navigation_snapshot(),
                               key0.hold_progress(now, kGlobalStopHoldMs),
                               &screenkey_demo.speed_trace()));
#else
    handle_keys(now);
    application.tick();
    key_interaction.synchronize(application.snapshot());
    screens::show(hmi::present(application.snapshot(), key_interaction.snapshot(),
                               key0.hold_progress(now, kGlobalStopHoldMs)));
#endif

    const hmi::BacklightUpdate backlight = backlight_controller.tick(now);
    if (backlight.changed) screens::set_brightness(backlight.duty);

    if (static_cast<uint32_t>(now - last_blink) >= 250) {
        last_blink = now;
        led.toggle();
    }

#if !RDJ_SCREENKEY_DEMO
    trace_status(now);
#endif
    screens::tick();
}

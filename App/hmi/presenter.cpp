#include "presenter.hpp"

#include "display_config.h"

#include <cstddef>
#include <cstdio>

namespace hmi {
namespace {

constexpr uint16_t kAmber = rgb565(255, 170, 20);
constexpr uint16_t kCyan = rgb565(30, 200, 255);
constexpr uint16_t kDim = rgb565(80, 80, 86);
constexpr int32_t kSparklineFullScaleMillirpm = 50;

template <std::size_t Size>
void copy_text(char (&destination)[Size], const char* source)
{
    std::size_t index = 0;
    while (index + 1 < Size && source[index] != '\0') {
        destination[index] = source[index];
        ++index;
    }
    destination[index] = '\0';
}

void set_key(KeyView& key, const char* header, const char* action, const char* detail,
             uint16_t accent, bool enabled = true, IconId icon = IconId::None,
             bool hold_available = false, uint16_t icon_color = 0)
{
    copy_text(key.header, header);
    copy_text(key.action, action);
    copy_text(key.detail, detail);
    key.accent = accent;
    key.icon_color = icon_color;
    key.icon = icon;
    key.enabled = enabled;
    key.hold_available = hold_available;
}

void set_speed_sparkline(KeyView& key, const platter_feedback::SpeedTrace* trace)
{
    if (trace == nullptr) return;
    const std::size_t count = trace->size() < SpeedSparkline::capacity
        ? trace->size() : SpeedSparkline::capacity;
    key.speed_sparkline.count = static_cast<uint8_t>(count);
    for (std::size_t index = 0; index < count; ++index) {
        int32_t deviation = trace->deviation_millirpm(index);
        if (deviation < -kSparklineFullScaleMillirpm)
            deviation = -kSparklineFullScaleMillirpm;
        if (deviation > kSparklineFullScaleMillirpm)
            deviation = kSparklineFullScaleMillirpm;
        key.speed_sparkline.samples[index] = static_cast<int8_t>(
            deviation * 127 / kSparklineFullScaleMillirpm);
    }
}

IconId transport_icon(turntable::State state)
{
    using turntable::State;
    switch (state) {
    case State::NeedsHome:
    case State::RaisingForInitialization:
    case State::HomingCarriage:
    case State::ParkingCarriage: return IconId::Home;
    case State::Idle:
    case State::Paused:
    case State::Interrupted: return IconId::Play;
    case State::Playing: return IconId::Pause;
    case State::Fault: return IconId::Warning;
    case State::Maintenance: return IconId::Settings;
    default: return IconId::Stop;
    }
}

const char* transport_action(turntable::State state, const turntable::ActionSet& actions)
{
    using turntable::State;
    switch (state) {
    case State::NeedsHome: return "INIT";
    case State::RaisingForInitialization:
    case State::HomingCarriage:
    case State::ParkingCarriage: return "CANCEL";
    case State::Idle: return "PLAY";
    case State::Playing: return "PAUSE";
    case State::Paused:
    case State::Interrupted: return "RESUME";
    case State::Fault:
        return actions.contains(turntable::Action::AcknowledgeFault) ? "ACK" : "WAIT";
    case State::StoppingPlatter:
    case State::ReturningHomeWithPlatter:
    case State::ReturningHomeWithoutPlatter: return "STOPPING";
    case State::Maintenance:
        return actions.contains(turntable::Action::Cancel) ? "CANCEL" : "WORKING";
    default: return "STOP";
    }
}

const char* transport_detail(turntable::State state)
{
    using turntable::State;
    switch (state) {
    case State::NeedsHome: return "TAP TO HOME";
    case State::RaisingForInitialization:
    case State::RaisingForPause:
    case State::RaisingForSpeedChange:
    case State::RaisingForStop: return "RAISING ARM";
    case State::HomingCarriage: return "HOMING";
    case State::ParkingCarriage: return "PARKING";
    case State::SpinningUpForPlay:
    case State::SpinningUpForResume: return "SPIN UP";
    case State::SeekingLeadIn: return "SEEKING";
    case State::LoweringForPlay:
    case State::LoweringForResume:
    case State::LoweringAfterSpeedChange: return "LOWERING ARM";
    case State::Playing:
    case State::Paused:
    case State::Interrupted: return "HOLD STOP";
    case State::ChangingSpeedForPlayback:
    case State::ChangingSpeedWhilePaused: return "SPEED CHANGE";
    case State::ReturningHomeWithPlatter:
    case State::ReturningHomeWithoutPlatter: return "RETURNING";
    case State::StoppingPlatter: return "PLATTER";
    case State::Maintenance: return "SERVICE";
    case State::Fault: return "SEE DETAILS";
    case State::Idle: return "";
    }
    return "";
}

const char* status_group(turntable::State state)
{
    using turntable::State;
    switch (state) {
    case State::NeedsHome:
    case State::RaisingForInitialization:
    case State::HomingCarriage:
    case State::ParkingCarriage: return "HOME";
    case State::Idle: return "IDLE";
    case State::Playing: return "PLAYING";
    case State::Paused: return "PAUSED";
    case State::Interrupted: return "INTERRUPT";
    case State::Fault: return "FAULT";
    case State::Maintenance: return "SERVICE";
    default: return "BUSY";
    }
}

const char* fault_source(turntable::FaultSource source)
{
    switch (source) {
    case turntable::FaultSource::Product: return "SYSTEM";
    case turntable::FaultSource::Platter: return "PLATTER";
    case turntable::FaultSource::Carriage: return "CARRIAGE";
    case turntable::FaultSource::Lift: return "ARM LIFT";
    case turntable::FaultSource::Diagnostic: return "DIAGNOSTIC";
    }
    return "UNKNOWN";
}

const char* fault_code(turntable::FaultCode code)
{
    using turntable::FaultCode;
    switch (code) {
    case FaultCode::None: return "NO FAULT";
    case FaultCode::PlatterDriver: return "DRIVER";
    case FaultCode::PlatterEncoder: return "ENCODER";
    case FaultCode::PlatterLockTimeout: return "LOCK TIMEOUT";
    case FaultCode::CarriageHomeTimeout: return "HOME TIMEOUT";
    case FaultCode::CarriageSeekTimeout: return "SEEK TIMEOUT";
    case FaultCode::CarriageReturnTimeout: return "RETURN TIMEOUT";
    case FaultCode::CarriageEncoder: return "ENCODER";
    case FaultCode::CarriageStall: return "STALL";
    case FaultCode::CarriageSoftwareLimit: return "SOFT LIMIT";
    case FaultCode::MaintenanceTimeout: return "MAINT TIMEOUT";
    case FaultCode::MaintenanceFailed: return "MAINT FAILED";
    case FaultCode::DiagnosticFailure: return "DIAG FAILED";
    case FaultCode::Unknown: return "UNKNOWN";
    }
    return "UNKNOWN";
}

const char* recovery_name(turntable::RecoveryPolicy recovery)
{
    switch (recovery) {
    case turntable::RecoveryPolicy::Retryable: return "RETRY";
    case turntable::RecoveryPolicy::RequiresCarriageHome: return "REHOME";
    case turntable::RecoveryPolicy::RequiresPowerCycle: return "POWER CYCLE";
    }
    return "UNKNOWN";
}

void measured_rpm(char (&text)[20], float rpm)
{
    const int32_t hundredths = static_cast<int32_t>(rpm * 100.0f + (rpm >= 0.0f ? 0.5f : -0.5f));
    const long whole = static_cast<long>(hundredths / 100);
    const long fraction = static_cast<long>(hundredths < 0
        ? -(hundredths % 100) : hundredths % 100);
    std::snprintf(text, sizeof(text), "%ld.%02ld RPM", whole, fraction);
}

View entering_diagnostic_view()
{
    View view;
    set_key(view.keys[0], "DIAGNOSTICS", "CANCEL", "SAFE ENTRY", kRed, true, IconId::Back);
    set_key(view.keys[1], "TONEARM", "RAISING", "PLEASE WAIT", kAmber, false, IconId::Home);
    set_key(view.keys[2], "CONTROL", "ENTERING", "PLEASE WAIT", kCyan, false,
            IconId::Settings);
    return view;
}

View normal_view(const turntable::ApplicationSnapshot& snapshot,
                 const platter_feedback::SpeedTrace* speed_trace)
{
    if (snapshot.state == turntable::ApplicationState::EnteringDiagnostic)
        return entering_diagnostic_view();

    const turntable::Snapshot& state = snapshot.turntable;
    View view;
    const IconId icon = transport_icon(state.state);
    uint16_t icon_color = 0;
    if (icon == IconId::Play) icon_color = kGreen;
    else if (icon == IconId::Stop) icon_color = kRed;
    set_key(view.keys[0], "TRANSPORT", transport_action(state.state, state.actions),
            transport_detail(state.state), kRed,
            state.state != turntable::State::StoppingPlatter
                && state.state != turntable::State::ReturningHomeWithPlatter
                && state.state != turntable::State::ReturningHomeWithoutPlatter,
            icon, state.actions.contains(turntable::Action::Stop), icon_color);

    char rpm[20];
    measured_rpm(rpm, state.measured_rpm);
    set_key(view.keys[1], "SPEED",
            state.selected_speed == turntable::RecordSpeed::Rpm45 ? "45 RPM" : "33 RPM",
            rpm, kAmber, state.actions.contains(turntable::Action::SelectSpeed));
    set_speed_sparkline(view.keys[1], speed_trace);
    set_key(view.keys[2], "SYSTEM",
            state.state == turntable::State::Fault ? "DETAILS" : "SETTINGS",
            state.home == turntable::HomeConfidence::Valid ? "HOME OK" : "NO HOME", kCyan,
            true, state.state == turntable::State::Fault ? IconId::Warning : IconId::Settings);
    return view;
}

const char* settings_item_name(SettingsItem item)
{
    switch (item) {
    case SettingsItem::SystemStatus: return "STATUS";
    case SettingsItem::Diagnostics: return "DIAGNOSTIC";
    case SettingsItem::Brightness: return "BRIGHTNESS";
    }
    return "UNKNOWN";
}

View settings_view(const turntable::ApplicationSnapshot& snapshot, SettingsItem item)
{
    View view;
    const bool stop_available = snapshot.turntable.actions.contains(turntable::Action::Stop);
    set_key(view.keys[0], "SETTINGS", "BACK", stop_available ? "HOLD STOP" : "", kRed,
            true, IconId::Back, stop_available);
    const bool diagnostics_enabled =
        snapshot.turntable.actions.contains(turntable::Action::EnterDiagnostics);
    const bool enabled = item != SettingsItem::Diagnostics || diagnostics_enabled;
    const char* detail = "SELECT";
    if (item == SettingsItem::Diagnostics && !diagnostics_enabled) detail = "STOP FIRST";
    if (item == SettingsItem::Brightness) detail = "AUTO DIM";
    IconId item_icon = IconId::Status;
    if (item == SettingsItem::Diagnostics) item_icon = IconId::Diagnostic;
    if (item == SettingsItem::Brightness) item_icon = IconId::Brightness;
    set_key(view.keys[1], "SELECT", settings_item_name(item), detail, kAmber, enabled, item_icon);
    set_key(view.keys[2], "BROWSE", "NEXT", "TAP", kCyan, true, IconId::Next);
    return view;
}

View status_view(const turntable::Snapshot& state,
                 const platter_feedback::SpeedTrace* speed_trace)
{
    View view;
    const bool stop_available = state.actions.contains(turntable::Action::Stop);
    set_key(view.keys[0], "STATUS", "BACK", stop_available ? "HOLD STOP" : "", kRed, true,
            IconId::Back, stop_available);
    set_key(view.keys[1], "SYSTEM", status_group(state.state),
            state.home == turntable::HomeConfidence::Valid ? "HOME OK" : "NO HOME", kAmber,
            true, IconId::Status);
    char rpm[20];
    measured_rpm(rpm, state.measured_rpm);
    set_key(view.keys[2], "PLATTER",
            state.selected_speed == turntable::RecordSpeed::Rpm45 ? "45 RPM" : "33 RPM",
            rpm, kCyan);
    set_speed_sparkline(view.keys[2], speed_trace);
    return view;
}

View diagnostic_confirmation_view(bool enabled)
{
    View view;
    set_key(view.keys[0], "DIAGNOSTICS", "CANCEL", "BACK", kRed, true, IconId::Back);
    set_key(view.keys[1], "CONFIRM", enabled ? "ENTER" : "BLOCKED",
            enabled ? "ARM RAISES" : "STOP FIRST", kAmber, enabled, IconId::Confirm);
    set_key(view.keys[2], "SAFETY", "LIMITS ON", "", kCyan, true, IconId::Diagnostic);
    return view;
}

View brightness_view()
{
    View view;
    set_key(view.keys[0], "BRIGHTNESS", "BACK", "SETTINGS", kRed, true, IconId::Back);
    set_key(view.keys[1], "DISPLAY", "AUTO DIM", "5 SEC IDLE", kAmber, true,
            IconId::Brightness);
    set_key(view.keys[2], "CONTROL", "SMOOTH", "FAST WAKE", kCyan, true,
            IconId::Confirm);
    return view;
}

View fault_details_view(const turntable::Snapshot& state)
{
    View view;
    set_key(view.keys[0], "FAULT", "BACK", "SAFE OUTPUTS", kRed, true, IconId::Back);
    set_key(view.keys[1], "SOURCE", fault_source(state.fault.source), fault_code(state.fault.code),
            kAmber, true, IconId::Warning);
    set_key(view.keys[2], "RECOVERY", recovery_name(state.fault.recovery),
            state.fault.invalidates_home ? "HOME LOST" : "HOME KEPT", kCyan, true,
            IconId::Home);
    return view;
}

View diagnostic_view(const turntable::ApplicationSnapshot& snapshot)
{
    const diagnostics::Snapshot& state = snapshot.diagnostic;
    View view;
    if (state.state == diagnostics::State::Fault) {
        set_key(view.keys[0], "DIAGNOSTIC", "ACK", "HOLD EXIT", kRed, true,
                IconId::Warning, true);
        set_key(view.keys[1], "COMMAND", "FAULT", "OUTPUTS SAFE", kAmber, false,
                IconId::Warning);
        set_key(view.keys[2], "CONTROL", "FAULT", "OUTPUTS SAFE", kCyan, false,
                IconId::Warning);
        return view;
    }
    if (state.state == diagnostics::State::Stopping
        || snapshot.state == turntable::ApplicationState::ExitingDiagnostic) {
        set_key(view.keys[0], "DIAGNOSTIC", "STOPPING", "PLEASE WAIT", kRed, false,
                IconId::Stop, false, kRed);
        set_key(view.keys[1], "COMMAND", "STOPPING", "PLEASE WAIT", kAmber, false,
                IconId::Stop, false, kRed);
        set_key(view.keys[2], "CONTROL", "STOPPING", "PLEASE WAIT", kCyan, false,
                IconId::Stop, false, kRed);
        return view;
    }

    const bool running = state.state == diagnostics::State::Running;
    const diagnostics::Action active = state.command.action;
    set_key(view.keys[0], "PLATTER",
            running && active == diagnostics::Action::OpenLoopSpin ? "STOP" : "SPIN",
            running ? "HOLD STOP" : "HOLD EXIT", kRed,
            !running || active == diagnostics::Action::OpenLoopSpin,
            running ? IconId::Stop : IconId::Spin, true,
            running ? kRed : 0);
    set_key(view.keys[1], "ENCODER",
            running && active == diagnostics::Action::EncoderAutoCal ? "CAL RUN" : "ALIGN",
            "HOLD CAL", kAmber,
            !running || active == diagnostics::Action::ElectricalAlign
                || active == diagnostics::Action::EncoderAutoCal,
            IconId::Encoder);
    const bool velocity_running =
        running && active == diagnostics::Action::ClosedLoopVelocity;
    const char* control_detail = "READY";
    if (velocity_running)
        control_detail = state.report.platter_calibrated ? "CLOSED LOOP" : "OPEN: RUN CAL";
    else if (running)
        control_detail = "RUNNING";
    set_key(view.keys[2], "CONTROL", velocity_running ? "STOP" : "LOOP", control_detail, kCyan,
            !running || active == diagnostics::Action::ClosedLoopVelocity,
            running ? IconId::Stop : IconId::Velocity);
    return view;
}

}  // namespace

View present(const turntable::ApplicationSnapshot& snapshot, NavigationSnapshot navigation,
             uint8_t transport_hold_progress,
             const platter_feedback::SpeedTrace* speed_trace)
{
    View view;
    if (snapshot.authority == turntable::ControlAuthority::Diagnostic) {
        view = diagnostic_view(snapshot);
        view.keys[0].hold_progress = view.keys[0].hold_available ? transport_hold_progress : 0;
        return view;
    }

    switch (navigation.mode) {
    case Mode::Primary: view = normal_view(snapshot, speed_trace); break;
    case Mode::SettingsBrowse: view = settings_view(snapshot, navigation.settings_item); break;
    case Mode::SystemStatus: view = status_view(snapshot.turntable, speed_trace); break;
    case Mode::DiagnosticConfirmation:
        view = diagnostic_confirmation_view(
            snapshot.turntable.actions.contains(turntable::Action::EnterDiagnostics));
        break;
    case Mode::Brightness: view = brightness_view(); break;
    case Mode::FaultDetails: view = fault_details_view(snapshot.turntable); break;
    }
    view.keys[0].hold_progress = view.keys[0].hold_available ? transport_hold_progress : 0;
    return view;
}

}  // namespace hmi

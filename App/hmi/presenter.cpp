#include "presenter.hpp"

#include "display_config.h"

namespace hmi {
namespace {

constexpr uint16_t kAmber = rgb565(255, 170, 20);
constexpr uint16_t kCyan = rgb565(30, 200, 255);
constexpr uint16_t kDim = rgb565(80, 80, 86);

const char* transport_action(turntable::State state)
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
    case State::Fault: return "ACK";
    case State::StoppingPlatter:
    case State::ReturningHomeWithPlatter:
    case State::ReturningHomeWithoutPlatter: return "STOPPING";
    default: return "STOP";
    }
}

const char* transport_detail(turntable::State state)
{
    using turntable::State;
    switch (state) {
    case State::HomingCarriage: return "HOMING";
    case State::ParkingCarriage: return "PARKING";
    case State::SpinningUpForPlay:
    case State::SpinningUpForResume: return "SPIN UP";
    case State::SeekingLeadIn: return "SEEKING";
    case State::Playing: return "HOLD STOP";
    case State::Paused: return "HOLD STOP";
    case State::Fault: return "DETAILS";
    default: return "";
    }
}

View normal_view(const turntable::ApplicationSnapshot& snapshot)
{
    const turntable::Snapshot& state = snapshot.turntable;
    View view;
    view.keys[0] = {"TRANSPORT", transport_action(state.state), transport_detail(state.state),
                    kRed, true};
    view.keys[1] = {"SPEED",
                    state.selected_speed == turntable::RecordSpeed::Rpm45 ? "45 RPM" : "33 RPM",
                    state.speed_locked ? "LOCKED" : "", kAmber,
                    state.actions.contains(turntable::Action::SelectSpeed)};
    view.keys[2] = {"SYSTEM", state.state == turntable::State::Fault ? "DETAILS" : "SETTINGS",
                    state.home == turntable::HomeConfidence::Valid ? "HOME OK" : "NO HOME", kCyan,
                    true};
    return view;
}

View diagnostic_view(const turntable::ApplicationSnapshot& snapshot)
{
    const diagnostics::Snapshot& state = snapshot.diagnostic;
    const bool running = state.state == diagnostics::State::Running;
    const diagnostics::Action active = state.command.action;
    View view;
    view.keys[0] = {"PLATTER", running && active == diagnostics::Action::OpenLoopSpin ? "STOP" : "SPIN",
                    "HOLD STOP", kRed, !running || active == diagnostics::Action::OpenLoopSpin};
    view.keys[1] = {"ENCODER",
                    running && active == diagnostics::Action::EncoderAutoCal ? "CAL RUN" : "ALIGN",
                    "HOLD CAL", kAmber,
                    !running || active == diagnostics::Action::ElectricalAlign
                        || active == diagnostics::Action::EncoderAutoCal};
    view.keys[2] = {"CONTROL",
                    running && active == diagnostics::Action::ClosedLoopVelocity ? "STOP" : "LOOP",
                    running ? "RUNNING" : "READY", kCyan,
                    !running || active == diagnostics::Action::ClosedLoopVelocity};

    if (state.state == diagnostics::State::Fault) {
        for (KeyView& key : view.keys) {
            key.action = "FAULT";
            key.detail = "HOLD STOP";
            key.accent = kRed;
            key.enabled = false;
        }
    }
    for (KeyView& key : view.keys)
        if (!key.enabled) key.accent = kDim;
    return view;
}

}  // namespace

View present(const turntable::ApplicationSnapshot& snapshot)
{
    return snapshot.authority == turntable::ControlAuthority::Diagnostic
        ? diagnostic_view(snapshot)
        : normal_view(snapshot);
}

}  // namespace hmi

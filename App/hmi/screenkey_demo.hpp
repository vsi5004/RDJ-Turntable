#pragma once

#include "interaction_controller.hpp"

#include <cstdint>

namespace hmi {

class ScreenKeyDemo {
public:
    explicit ScreenKeyDemo(InteractionConfig config = {})
        : interaction_config_(config), interaction_(config) {}

    void reset(uint32_t now_ms = 0);
    void handle(Key key, Gesture gesture, uint32_t now_ms);
    void tick(uint32_t now_ms);

    const turntable::ApplicationSnapshot& application_snapshot() const { return application_; }
    NavigationSnapshot navigation_snapshot() const { return interaction_.snapshot(); }

private:
    enum class Deadline : uint8_t {
        None,
        BeginHoming,
        BeginParking,
        FinishInitialization,
        BeginSeeking,
        BeginLoweringForPlay,
        FinishPlay,
        FinishPause,
        FinishResume,
        BeginSpeedChange,
        BeginLoweringAfterSpeedChange,
        FinishSpeedChange,
        BeginReturn,
        BeginPlatterStop,
        FinishStop,
        FinishReturnWithoutPlatter,
        FinishDiagnosticEntry,
        FinishDiagnosticCommand,
        FinishDiagnosticAbort,
        FinishDiagnosticExit,
    };

    void apply(const Intent& intent, uint32_t now_ms);
    void apply_turntable_event(const turntable::Event& event, uint32_t now_ms);
    void set_state(turntable::State state);
    void update_actions();
    void schedule(Deadline deadline, uint32_t duration_ms, uint32_t now_ms);
    void advance(Deadline deadline, uint32_t now_ms);
    void inject_product_fault();
    void inject_diagnostic_fault();

    InteractionConfig interaction_config_{};
    InteractionController interaction_;
    turntable::ApplicationSnapshot application_{};
    Deadline deadline_ = Deadline::None;
    uint32_t deadline_started_ms_ = 0;
    uint32_t deadline_duration_ms_ = 0;
};

}  // namespace hmi

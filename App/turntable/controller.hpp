#pragma once

#include "interfaces.hpp"

namespace turntable {

class Controller {
public:
    Controller(IClock& clock, IPlatter& platter, ITonearmCarriage& carriage,
               ITonearmLift& lift, ControllerConfig config = {},
               IMaintenanceExecutor* maintenance = nullptr);

    void handle(const Event& event);
    void tick();
    void update_observations(const Observations& observations);

    State state() const { return state_; }
    RecordSpeed selected_speed() const { return selected_speed_; }
    uint32_t active_operation_id() const { return active_operation_id_; }
    Snapshot snapshot() const;

    bool can_enter_diagnostics() const;
    uint32_t prepare_for_diagnostics();
    void restore_after_diagnostics(bool invalidate_home = false);

private:
    void transition_to(State next);
    void on_enter(State state);
    void handle_stop();
    void handle_speed_change(RecordSpeed speed);
    void handle_deadline();
    void enter_fault(FaultRecord fault);
    FaultRecord deadline_fault() const;
    uint32_t begin_operation(uint32_t timeout_ms);
    bool matches_active(const Event& event) const;
    void clear_deadline();
    ActionSet available_actions() const;

    IClock& clock_;
    IPlatter& platter_;
    ITonearmCarriage& carriage_;
    ITonearmLift& lift_;
    IMaintenanceExecutor* maintenance_;
    ControllerConfig config_;

    State state_ = State::NeedsHome;
    RecordSpeed selected_speed_ = RecordSpeed::Rpm33;
    Observations observations_{};
    FaultRecord fault_{};
    bool fault_condition_clear_ = false;
    bool fault_raise_settled_ = false;
    MaintenanceOperation maintenance_operation_ = MaintenanceOperation::PlatterAlignment;

    uint32_t next_operation_id_ = 0;
    uint32_t active_operation_id_ = 0;
    uint32_t deadline_started_ms_ = 0;
    uint32_t deadline_duration_ms_ = 0;
    bool deadline_armed_ = false;
};

}  // namespace turntable

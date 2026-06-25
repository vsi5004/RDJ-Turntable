#pragma once

#include "assets.hpp"
#include "control/platter_feedback.hpp"
#include "interaction_controller.hpp"

#include <cstddef>
#include <cstdint>

namespace hmi {

struct SpeedSparkline {
    static constexpr std::size_t capacity = platter_feedback::SpeedTrace::capacity;

    int8_t samples[capacity]{}; // normalized to -127..127 around target RPM
    uint8_t count = 0;
};

struct KeyView {
    char header[16]{};
    char action[16]{};
    char detail[20]{};
    uint16_t accent = 0;
    uint16_t icon_color = 0;
    IconId icon = IconId::None;
    SpeedSparkline speed_sparkline{};
    uint8_t hold_progress = 0;
    bool enabled = true;
    bool hold_available = false;
};

struct View {
    KeyView keys[3]{};
};

/* board_temp_c sentinel for "no reading yet" (mirrors board_temp::kUnavailable). */
constexpr int16_t kTempUnavailable = INT16_MIN;

View present(const turntable::ApplicationSnapshot& snapshot,
             NavigationSnapshot navigation = {}, uint8_t transport_hold_progress = 0,
             const platter_feedback::SpeedTrace* speed_trace = nullptr,
             int16_t board_temp_c = kTempUnavailable);

}  // namespace hmi

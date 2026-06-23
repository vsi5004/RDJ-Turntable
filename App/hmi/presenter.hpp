#pragma once

#include "assets.hpp"
#include "interaction_controller.hpp"

#include <cstdint>

namespace hmi {

struct KeyView {
    char header[16]{};
    char action[16]{};
    char detail[20]{};
    uint16_t accent = 0;
    IconId icon = IconId::None;
    uint8_t hold_progress = 0;
    bool enabled = true;
    bool hold_available = false;
};

struct View {
    KeyView keys[3]{};
};

View present(const turntable::ApplicationSnapshot& snapshot,
             NavigationSnapshot navigation = {}, uint8_t transport_hold_progress = 0);

}  // namespace hmi

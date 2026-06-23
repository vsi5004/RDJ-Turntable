#pragma once

#include "interaction_controller.hpp"

#include <cstdint>

namespace hmi {

struct KeyView {
    char header[16]{};
    char action[16]{};
    char detail[20]{};
    uint16_t accent = 0;
    bool enabled = true;
};

struct View {
    KeyView keys[3]{};
};

View present(const turntable::ApplicationSnapshot& snapshot,
             NavigationSnapshot navigation = {});

}  // namespace hmi

#pragma once

#include "turntable/application_controller.hpp"

#include <cstdint>

namespace hmi {

struct KeyView {
    const char* header = "";
    const char* action = "";
    const char* detail = "";
    uint16_t accent = 0;
    bool enabled = true;
};

struct View {
    KeyView keys[3]{};
};

View present(const turntable::ApplicationSnapshot& snapshot);

}  // namespace hmi

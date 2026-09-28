#pragma once

namespace lamium::ui {
struct SettingsNavigation {
    int current = 0;
    int remembered = 0;

    void reopenNormal() { current = remembered; }
    void select(int index, bool temporary = false) {
        current = index;
        if (!temporary) remembered = index;
    }
};
}

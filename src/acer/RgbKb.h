#pragma once

namespace predator {

struct RgbSettings {
    int mode = 0;  // 0 static, 1 breath, 2 neon, 3 wave, 4 shift, 5 zoom
    int speed = 3;
    int brightness = 80;
    int direction = 1;
    int r = 255;
    int g = 80;
    int b = 0;
};

bool ApplyKeyboardRgb(const RgbSettings& s);

}  // namespace predator

#include "acer/RgbKb.h"

#include "acer/AcerService.h"
#include "acer/GamingWmi.h"
#include "common/Log.h"

#include <vector>

namespace predator {

bool ApplyKeyboardRgb(const RgbSettings& s) {
    GamingWmi wmi;
    bool ok = false;
    if (wmi.Ok() || wmi.Connect()) {
        wmi.SetGamingLed(8ull | (15ull << 40));
        if (s.mode == 0) {
            const uint8_t zones[] = {0x01, 0x02, 0x04, 0x08};
            for (uint8_t z : zones) {
                if (wmi.SetKeyboardStaticZone(z, static_cast<uint8_t>(s.r),
                                              static_cast<uint8_t>(s.g),
                                              static_cast<uint8_t>(s.b))) {
                    ok = true;
                }
            }
            std::vector<uint8_t> payload(16, 0);
            payload[2] = static_cast<uint8_t>(s.brightness);
            payload[9] = 1;
            if (wmi.SetKeyboardBacklight(payload)) {
                ok = true;
            }
        } else {
            std::vector<uint8_t> payload(16, 0);
            payload[0] = static_cast<uint8_t>(s.mode);
            payload[1] = static_cast<uint8_t>(s.speed);
            payload[2] = static_cast<uint8_t>(s.brightness);
            payload[3] = (s.mode == 3) ? 8 : 0;
            payload[4] = static_cast<uint8_t>(s.direction);
            payload[5] = static_cast<uint8_t>(s.r);
            payload[6] = static_cast<uint8_t>(s.g);
            payload[7] = static_cast<uint8_t>(s.b);
            payload[9] = 1;
            if (wmi.SetKeyboardBacklight(payload)) {
                ok = true;
            }
        }
        if (ok) {
            Log("RGB applied via WMI");
        }
    }

    if (s.mode == 0) {
        if (AcerService::SetLightingZones(s.r, s.g, s.b, s.brightness)) {
            Log("RGB static zones applied via AcerService");
            ok = true;
        }
    } else if (AcerService::SetLightingEffect(s.mode, s.r, s.g, s.b, s.brightness, s.speed,
                                              s.direction)) {
        Log("RGB effect applied via AcerService");
        ok = true;
    }
    return ok;
}

}  // namespace predator

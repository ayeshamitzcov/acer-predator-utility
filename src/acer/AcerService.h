#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace predator {

class AcerService {
public:
    static bool Available();
    static std::optional<std::string> Query(const std::string& function);
    static bool Set(const std::string& function, const std::string& json_body);
    static bool SetOperatingMode(uint8_t mode);
    static bool SetLightingJson(const std::string& json_object);
    static bool SetLightingZones(int r, int g, int b, int brightness);
    static bool SetLightingEffect(int mode, int r, int g, int b, int brightness, int speed,
                                  int direction);
    static bool SetLcdOverdrive(bool enable);
};

}  // namespace predator

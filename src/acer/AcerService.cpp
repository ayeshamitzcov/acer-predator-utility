#include "acer/AcerService.h"

#include "common/Log.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace predator {
namespace {

constexpr uint16_t kCommandPort = 46933;
constexpr uint32_t kGetUpdated = 20;
constexpr uint32_t kSetDevice = 100;

bool SendPacket(uint32_t packet_id, const std::string& json, std::string* response) {
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        return false;
    }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(kCommandPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    DWORD timeout = 800;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return false;
    }
    std::vector<char> pkt(8 + json.size());
    memcpy(pkt.data(), "ACER", 4);
    memcpy(pkt.data() + 4, &packet_id, 4);
    memcpy(pkt.data() + 8, json.data(), json.size());
    if (send(s, pkt.data(), static_cast<int>(pkt.size()), 0) < 0) {
        closesocket(s);
        return false;
    }
    char buf[4096];
    const int n = recv(s, buf, sizeof(buf) - 1, 0);
    closesocket(s);
    if (n > 8 && response) {
        buf[n] = 0;
        *response = std::string(buf + 8, buf + n);
    }
    return n > 8;
}

}  // namespace

bool AcerService::Available() {
    std::string resp;
    return Query("OPERATING_MODE").has_value() || Query("LIGHTING").has_value();
}

std::optional<std::string> AcerService::Query(const std::string& function) {
    const std::string json = std::string("{\"Function\":\"") + function + "\"}";
    std::string resp;
    if (!SendPacket(kGetUpdated, json, &resp)) {
        return std::nullopt;
    }
    return resp;
}

bool AcerService::Set(const std::string& function, const std::string& json_body) {
    std::string json = "{\"Function\":\"" + function + "\"";
    if (!json_body.empty()) {
        json += ",";
        json += json_body;
    }
    json += "}";
    std::string resp;
    Log(std::string("AcerService SET ") + function);
    return SendPacket(kSetDevice, json, &resp);
}

bool AcerService::SetOperatingMode(uint8_t mode) {
    char body[64];
    snprintf(body, sizeof(body), "\"Value\":%u", static_cast<unsigned>(mode));
    return Set("OPERATING_MODE", body);
}

bool AcerService::SetLightingJson(const std::string& json_object) {
    std::string resp;
    const std::string json = json_object;
    Log("AcerService SET LIGHTING");
    return SendPacket(kSetDevice, json, &resp);
}

int ScaleBrightness(int brightness) {
    if (brightness <= 0) {
        return 0;
    }
    int v = brightness / 20 + 1;
    if (v < 1) {
        v = 1;
    }
    if (v > 5) {
        v = 5;
    }
    return v;
}

bool AcerService::SetLightingZones(int r, int g, int b, int brightness) {
    char json[1024];
    snprintf(json, sizeof(json),
             "{\"Function\":\"LIGHTING\",\"Parameter\":{\"device\":1,\"effect\":\"STATIC\","
             "\"speed\":5,\"duration\":3,\"direction\":0,\"brightness\":%d,"
             "\"color\":\"#ffffff\",\"LEDs\":["
             "{\"LED_id\":0,\"color\":\"#%02x%02x%02x\",\"status\":1},"
             "{\"LED_id\":1,\"color\":\"#%02x%02x%02x\",\"status\":1},"
             "{\"LED_id\":2,\"color\":\"#%02x%02x%02x\",\"status\":1},"
             "{\"LED_id\":3,\"color\":\"#%02x%02x%02x\",\"status\":1}"
             "],\"subindex\":{\"1\":\"STATIC\",\"2\":\"STATIC\"},\"colortype\":1}}",
             ScaleBrightness(brightness), r, g, b, r, g, b, r, g, b, r, g, b);
    return SetLightingJson(json);
}

bool AcerService::SetLightingEffect(int mode, int r, int g, int b, int brightness, int speed,
                                    int direction) {
    const char* effects[] = {"STATIC", "BREATHING", "NEON", "WAVE", "SHIFTING", "ZOOM"};
    if (mode < 0 || mode > 5) {
        mode = 0;
    }
    const char* effect = effects[mode];
    const char* sub2 = (mode == 3) ? "NEON" : ((mode == 0) ? "STATIC" : "STATIC");
    char json[768];
    snprintf(json, sizeof(json),
             "{\"Function\":\"LIGHTING\",\"Parameter\":{\"device\":0,\"effect\":\"%s\","
             "\"speed\":%d,\"duration\":3,\"direction\":%d,\"brightness\":%d,"
             "\"color\":\"#%02x%02x%02x\",\"colortype\":1,"
             "\"subindex\":{\"1\":\"%s\",\"2\":\"%s\"}}}",
             effect, speed < 1 ? 1 : speed, direction, ScaleBrightness(brightness), r, g, b, effect,
             sub2);
    return SetLightingJson(json);
}

bool AcerService::SetLcdOverdrive(bool enable) {
    char body[64];
    snprintf(body, sizeof(body), "\"Parameter\":{\"status\":%d}", enable ? 1 : 0);
    return Set("LCD_OVERDRIVE", body);
}

}  // namespace predator

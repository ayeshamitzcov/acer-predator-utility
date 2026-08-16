#pragma once

#include <string>
#include <string_view>

namespace predator {

void LogInit();
void Log(std::string_view message);
std::wstring AppDataDir();

}  // namespace predator

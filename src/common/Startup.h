#pragma once

namespace predator {

// Logon task with highest privileges so the admin exe does not UAC every boot.
bool SetRunAtStartup(bool enable, bool minimized);

}  // namespace predator

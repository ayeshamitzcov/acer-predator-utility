# Predator Utility

I made this because the PredatorSense app is too clunky and power hungry and didn't have much customization.

It's a small Windows app I run on my Predator PT315-52 (Triton 300, i7-10750H / RTX 2070 Max-Q) instead of Acer's software. It talks to the same firmware WMI (`AcerGamingFunction`) PredatorSense uses, so fans, modes, and RGB still work without that whole stack sitting in the background.

## Modes

- **Quiet** — auto fans, stock clocks
- **Normal** — a bit more fan, stock clocks
- **ESports** — high refresh, overdrive, GPU offset, no CPU overclock (it just runs too hot)
- **Turbo** — BIOS turbo OC, GPU +125/+250, max fans
- **Battery saver** — 60 Hz, dimmer, RTX off, CPU capped

Unplug → battery saver. Plug in → ESports. You can still pick a mode yourself; it sticks until the cable changes.

Predator key shows/hides the window. Predator+1–5 jumps modes. Turbo key toggles Turbo and Normal.

## Install

Grab `PredatorUtility-Setup-0.1.0.exe` from [Releases](https://github.com/ayeshamitzcov/acer-predator-utility/releases). It asks for Administrator, puts the app in Program Files, then installs PawnIO (CPU watts) and the VC++ runtime if you don't have them.

Start with Windows is on by default (logon task, no UAC every boot). Settings has a checkbox to start minimized in the tray.

The installer script is in `installer/` (Inno Setup + a small PowerShell deps script). To build it yourself:

```powershell
.\scripts\Build-Installer.ps1
```

Output lands in `dist\`.

## Build

Visual Studio with the C++ workload, CMake 3.20+, Git (Dear ImGui gets fetched).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If you've got VS 2026, use `"Visual Studio 18 2026"` instead.

- `build/Release/PredatorUtility.exe` — the app (needs Administrator)
- `build/Release/predator-probe.exe` — dumps what WMI actually exposes

Settings/logs land in `%APPDATA%\PredatorLite\`.

## First run

Uninstall PredatorSense if it's still around. It steals the Predator key and fights you on fans/modes.

`scripts/Disable-PredatorSenseBloat.ps1` is there if you only want the service killed.

CPU watts need [PawnIO](https://pawnio.eu/) installed. The setup exe does that for you.

If fans go stupid, exit the app (it puts them back on auto) or reboot.

## RGB

Keyboard lighting goes through WMI. If you still have Acer's lighting service, it can use that too. Don't disable `AcerLightingService` unless you're fine with RGB dying.

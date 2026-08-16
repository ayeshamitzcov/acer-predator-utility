# Disabling PredatorSense bloat (keep RGB-capable services)

PredatorSense is a stack of UI + background services. This app talks to firmware WMI directly, so the heavy UI is not required.

## Disable (battery / conflict)

Safe to disable so PredatorSense does not override fans/modes:

| Name | Typical service | Why |
| --- | --- | --- |
| Predator Service | `PSSvc` | Owns PredatorSense settings; fights this app |
| PredatorSense launcher task | `PredatorSenseLauncher` | Relaunches the bloated UI |
| Acer CC / DI agents | `AcerCCAgentSvis`, `AcerDIAgentSvis` | Unused by this app |
| Device enabling v2 | `AcerDeviceEnablingServiceV2` | Unused here |

On this PT315-52, `PSSvc` (display name **Predator Service**) is the one that was actually installed.

## Keep if you want RGB / extras

| Name | Why |
| --- | --- |
| `AcerServiceSvc` | TCP `127.0.0.1:46933` lighting + some mode commands |
| `AcerLightingService` | Keyboard / logo lighting |
| `ASMSvc` | Some models use this for charge limit |
| `AcerQAAgentSvis` | Physical mode-key (optional) |

Fan control, thermal modes, and sensors work **without** PredatorSense if `AcerGamingFunction` is in `root\WMI` (firmware).

## Scripts

- `scripts/Disable-PredatorSenseBloat.ps1` — stop/disable the conflicting services (admin)
- `scripts/Restore-AcerServices.ps1` — re-enable them

Reboot after the first disable if PredatorSense still pops up.

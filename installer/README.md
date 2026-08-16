# Installer source

Everything here is part of the repo.

- `PredatorUtility.iss` — Inno Setup script. Requires Administrator. Copies the app into Program Files and creates Start Menu shortcuts.
- `Install-Dependencies.ps1` — run by the setup after files are copied. Downloads the official [PawnIO](https://github.com/namazso/PawnIO.Setup) installer and the MS VC++ x64 runtime if needed.
- `welcome.txt` — shown before install.

Compile with `..\scripts\Build-Installer.ps1` (needs Inno Setup 6). The `.exe` it spits out is a build artifact, not committed; attach it to a GitHub Release (see `github-release-workflow.yml` if you want CI to do that).

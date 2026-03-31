# YUView-ML Windows Installer

Builds a professional Windows installer for YUView-ML using [Inno Setup 6](https://jrsoftware.org/isinfo.php).

## Prerequisites

1. **Build YUView-ML** first by running `test_windows_build.ps1` from a Developer PowerShell.
   This produces the release folder at `build_test/YUViewSimpleRelease/`.

2. **Install Inno Setup 6** (free) from https://jrsoftware.org/isdl.php.
   The default install location is detected automatically.

## Build the installer

From the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1
```

Or specify a custom release folder:

```powershell
powershell -ExecutionPolicy Bypass -File installer\build_installer.ps1 -ReleaseDir "C:\path\to\release"
```

## Output

The installer is written to `installer/Output/YUView-ML-Setup.exe`.

## What the installer does

- Installs YUView-ML and all Qt runtime dependencies to `C:\Program Files\YUView-ML\`
- Shows the GPL v3 license agreement
- Optionally installs the Visual C++ Runtime (recommended for machines without it)
- Creates a Start Menu shortcut and optional Desktop shortcut
- Registers a clean uninstaller in Windows Add/Remove Programs

# Building YUView on Windows

This guide provides step-by-step instructions to build YUView and run tests on Windows.

## Prerequisites

1.  **Visual Studio 2022**: Install the Community edition (or Professional/Enterprise).
    *   Ensure you select the **"Desktop development with C++"** workload.
2.  **Qt 6**: Install Qt 6 (version 6.9.0 is used in CI, but recent 6.x versions should work).
    *   You can use the [Qt Online Installer](https://www.qt.io/download-qt-installer).
    *   Ensure the **MSVC 2019/2022 64-bit** component is selected.
3.  **Git**: Install Git for Windows.
4.  **Jom (Optional)**: `jom` is a replacement for `nmake` that supports parallel builds.
    *   Can be installed via Chocolatey: `choco install jom`.
    *   If not using `jom`, you can use `nmake` (included with VS).

## Build Steps

### 1. Open Developer Environment

**Crucial Step**: You must use the **Developer PowerShell for VS 2022** (or "x64 Native Tools Command Prompt for VS 2022").
*   Search for "Developer PowerShell for VS 2022" in the Windows Start menu.
*   This ensures all MSVC compilers and tools are in your PATH.

### 2. Clone the Repository

Clone the repository including submodules:

```powershell
git clone --recursive https://github.com/IENT/YUView.git
cd YUView
```

### 3. Setup Dependencies (Optional)

For full functionality (HEVC decoding), download `libde265`:
*   Download `libde265.dll` from [here](https://github.com/ChristianFeldmann/libde265/releases/download/v1.1/libde265.dll).
*   Place it in the root of the repository or in your build output directory later.

### 4. Configure and Build

Create a build directory and run `qmake`.

*Note: Ensure `qmake` is in your PATH. If not, add the path to your Qt bin directory (e.g., `C:\Qt\6.9.0\msvc2019_64\bin`) to your system PATH or use the full path.*

```powershell
mkdir build
cd build

# Configure with Unit Tests enabled
qmake CONFIG+=UNITTESTS ..

# Build using jom (recommended)
jom

# OR Build using nmake (standard)
nmake
```

### 5. Run Unit Tests

After a successful build, run the unit tests to verify everything is working correctly.

```powershell
.\YUViewUnitTest\YUViewUnitTest.exe
# OR depending on configuration
.\YUViewUnitTest\release\YUViewUnitTest.exe
```

### 6. Run YUView

The main application executable will be in `YUViewApp` or `YUViewApp\release`.

```powershell
.\YUViewApp\YUView.exe
```

## Troubleshooting

*   **"qmake is not recognized"**: Add the path to `qmake.exe` to your PATH environment variable.
*   **"cl is not recognized"**: You are likely not running in the **Developer PowerShell for VS 2022**.
*   **Missing DLLs**: If running the executable fails due to missing DLLs, you can run `windeployqt` to copy necessary Qt DLLs to your build folder:
    ```powershell
    windeployqt .\YUViewApp\release\YUView.exe
    ```

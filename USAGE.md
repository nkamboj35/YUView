# YUView-ML Usage Guide

YUView-ML is a customized build of [YUView](https://github.com/IENT/YUView) with extended
support for viewing output from ML pre-processing pipelines. It adds native floating-point
format support (FP16, BF16, FP32) and the inverse mean/scale controls needed to reconstruct
visible images from pre-processed data.

---

## Table of Contents

1. [Background: ML Pre-Processing Visualization](#background-ml-pre-processing-visualization)
2. [Supported Floating-Point Formats](#supported-floating-point-formats)
3. [Opening Files](#opening-files)
4. [Using Scale and Mean to Reverse Pre-Processing](#using-scale-and-mean-to-reverse-pre-processing)
5. [Custom Format Dialog](#custom-format-dialog)
6. [File Naming Conventions](#file-naming-conventions)
7. [Building from Source](#building-from-source)
8. [Release Notes](#release-notes)

---

## Background: ML Pre-Processing Visualization

Machine learning inference pipelines commonly pre-process input images before feeding them
to a model. A typical pre-processing stage performs two operations on each pixel channel:

```
pre_processed_value = (original_pixel - mean) * scale
```

For example, with ImageNet-style normalization (mean = 123.675, scale = 1/58.395 for the
red channel), a red pixel value of 180 becomes:

```
(180 - 123.675) * (1/58.395) = 0.9646
```

The output of such a pipeline is stored in a floating-point format (FP16, BF16, or FP32)
as raw binary data with a known resolution and channel layout (e.g., `BGR planar`).

**To visualize this data**, the inverse operation must be applied:

```
display_value = (pre_processed_value / scale) + mean
```

YUView-ML performs exactly this inverse operation in its display pipeline. You provide the
`Scale` and `Mean` values in the right-hand properties panel, and the viewer applies the
formula above to convert each floating-point sample back to a displayable 0-255 range.

### The Math in Code

The core conversion lives in `ConversionRGB.cpp`, function `finalizeFloatSample`:

```cpp
int finalizeFloatSample(double value, double scale, double mean, bool invert)
{
  const auto effectiveScale = (|scale| < epsilon) ? 1.0 : scale;
  const auto adjusted       = (value / effectiveScale) + mean;
  // ... clip to [0, 255], round, optionally invert
}
```

For each pixel sample (R, G, or B), the viewer:
1. Reads the raw float value from the file
2. Divides by `Scale` (the value you enter in the UI)
3. Adds `Mean` (the value you enter in the UI)
4. Clips the result to [0, 255] and rounds to an integer
5. Optionally inverts (255 - value) if the Invert checkbox is checked

---

## Supported Floating-Point Formats

Three floating-point sample types are supported alongside the standard integer types:

| Type | Bits | Description |
|------|------|-------------|
| **FP16** | 16 | IEEE 754 half-precision floating point |
| **BF16** | 16 | Brain Floating Point (wider exponent range, less mantissa precision) |
| **FP32** | 32 | IEEE 754 single-precision floating point |

### Layout Variants

Each type supports three layout variants:

| Layout | Description | Example |
|--------|-------------|---------|
| **Packed (interleaved)** | Channels are interleaved per pixel: `R0G0B0 R1G1B1 ...` | `RGB 16bit FP16` |
| **Planar** | Each channel stored as a contiguous plane: `RRR...GGG...BBB...` | `RGB 16bit planar FP16` |
| **4th channel ignored (x)** | Four interleaved channels, 4th is padding: `R0G0B0X0 R1G1B1X1 ...` | `RGBX 16bit FP16` |

### Complete Preset Table

| Layout | BF16 | FP16 | FP32 |
|--------|------|------|------|
| RGB (packed) | `RGB 16bit BF16` | `RGB 16bit FP16` | `RGB 32bit FP32` |
| BGR (packed) | `BGR 16bit BF16` | `BGR 16bit FP16` | `BGR 32bit FP32` |
| RGBP (planar) | `RGB 16bit planar BF16` | `RGB 16bit planar FP16` | `RGB 32bit planar FP32` |
| BGRP (planar) | `BGR 16bit planar BF16` | `BGR 16bit planar FP16` | `BGR 32bit planar FP32` |
| RGBx (4th ignored) | `RGBX 16bit BF16` | `RGBX 16bit FP16` | `RGBX 32bit FP32` |
| BGRx (4th ignored) | `BGRX 16bit BF16` | `BGRX 16bit FP16` | `BGRX 32bit FP32` |

All 18 combinations are available in the **RGB Format** dropdown.

---

## Opening Files

### Automatic Detection

YUView-ML uses filename hints to automatically detect the correct format:

- **Float type:** Include `fp16`, `bf16`, or `fp32` in the filename.
- **Channel order:** Include `rgb`, `bgr`, `rgbx`, `bgrx`, `rgbp`, or `bgrp`.
- **Data layout:** Include `planar` or `packed` to override the default packed layout.
- **Resolution:** Include `WxH` (e.g., `640x480`).

**Examples:**

| Filename | Detected as |
|----------|-------------|
| `output_640x480_bf16_bgrx.raw` | BGR with ignored alpha, BF16, 640x480 |
| `frame_1920x1080_fp16_rgb_planar.raw` | RGB planar, FP16, 1920x1080 |
| `data_320x240.fp32` | RGB packed, FP32, 320x240 |
| `image.bgr` | BGR packed, 8-bit integer (no float hint) |

### The `.raw` Extension

Files with the `.raw` extension are handled specially:

- If the filename contains any float/RGB hints (`fp16`, `bf16`, `fp32`, `rgb`, `bgr`,
  `rgbp`, `bgrp`, `rgbx`, `bgrx`), the file opens as **RGB** with the appropriate format.
- If no hints are found, the file opens as **YUV** (backward compatible with raw Bayer data).

### Manual Format Selection

If autodetection picks the wrong format, you can disable it:

1. Go to **Settings > General** and uncheck "Automatically detect file type".
2. When opening a file, a dialog appears asking you to choose between "Raw YUV File"
   and "Raw RGB File".
3. After opening, select the exact format from the **RGB Format** dropdown in the
   properties panel.

---

## Using Scale and Mean to Reverse Pre-Processing

When viewing output from an ML pre-processing IP, you need to enter the same mean and
scale values that were used during pre-processing. The viewer applies the inverse
transformation to recover the original pixel values for display.

### Step-by-Step

1. **Open the pre-processed file** (e.g., `output_480x320_bf16_bgrx_le.raw`).
2. **Select the correct RGB format** from the dropdown (or let autodetection handle it).
3. **Enter the Scale values** used during pre-processing:
   - `Scale R`, `Scale G`, `Scale B` in the properties panel.
   - These correspond to the multiplication factors applied during pre-processing.
   - Default is `1.0` (no scaling).
4. **Enter the Mean values** used during pre-processing:
   - `Mean R`, `Mean G`, `Mean B` in the properties panel.
   - These correspond to the offsets subtracted during pre-processing.
   - Default is `0.0` (no offset).
5. The image should now display correctly.

### Example: ImageNet Normalization

If your pre-processing pipeline uses ImageNet-style normalization:

```
pre_processed = (pixel - mean) * scale
```

With standard values:

| Channel | Mean | Scale (1/std) |
|---------|------|---------------|
| R | 123.675 | 0.01712 (1/58.395) |
| G | 116.280 | 0.01751 (1/57.120) |
| B | 103.530 | 0.01743 (1/57.375) |

Enter in YUView-ML:

| Field | R | G | B |
|-------|---|---|---|
| **Scale** | 0.01712 | 0.01751 | 0.01743 |
| **Mean** | 123.675 | 116.280 | 103.530 |

The viewer computes: `display = (raw_value / scale) + mean`, recovering the original
pixel values.

### Per-Channel Controls

Each channel (R, G, B, A) has independent controls:

- **Scale** (0.0 to 1000.0, 4 decimal places): The divisor applied to the raw sample.
  A scale of 0 is treated as 1.0 (no division).
- **Mean** (-1000.0 to 1000.0, 4 decimal places): The offset added after division.
- **Invert**: When checked, the final 8-bit value is inverted (255 - value).

### Common Pre-Processing Configurations

| Pipeline | Scale (all channels) | Mean (all channels) |
|----------|---------------------|---------------------|
| Raw float [0.0, 1.0] to [0, 255] | 1.0 | 0.0 (default) |
| Raw float [0.0, 1.0], prescaled by 1/255 | 0.003922 (1/255) | 0.0 |
| ImageNet (per-channel) | See table above | See table above |
| Simple mean subtraction (mean=128) | 1.0 | 128.0 |

---

## Custom Format Dialog

For formats not in the preset dropdown, click **Custom...** at the bottom of the
**RGB Format** dropdown. The dialog provides:

- **RGB Order**: Channel ordering (RGB, BGR, GBR, etc.)
- **Bit Depth**: Auto-set for float types, user-configurable for integer types.
- **Endianness**: Big or Little endian.
- **Sample Type**: Select `UInt`, `Int`, `FP16`, `BF16`, or `FP32`. Selecting a float type
  automatically locks the bit depth to the correct value (16 or 32).
- **Planar**: Check to use planar layout instead of interleaved.
- **Alpha Channel**: Enable and choose position (before/after RGB data).
- **Ignore alpha**: Check to include a 4th channel in the file that is discarded during
  display (e.g., RGBx padding).

---

## File Naming Conventions

For reliable autodetection, use delimiters (`_`, `.`, `-`) to separate tokens in filenames:

```
<description>_<width>x<height>_<format>_<channel>_<endian>.<extension>
```

**Recommended extensions:**

| Extension | Default handler |
|-----------|----------------|
| `.rgb`, `.bgr` | RGB (integer unless fp hint in name) |
| `.fp16`, `.bf16`, `.fp32` | RGB with matching float type |
| `.raw` | YUV (unless filename contains float/RGB hints) |
| `.yuv` | YUV |

---

## Building from Source

See [windows_build.md](windows_build.md) for detailed Windows build instructions.

**Quick start:**

```powershell
# From Developer PowerShell for VS 2022:
.\test_windows_build.ps1
```

This builds the app, runs all unit tests, and creates a portable release folder at
`build_test/YUViewSimpleRelease/`.

**To create an installer:**

```powershell
# Requires Inno Setup 6 (https://jrsoftware.org/isdl.php)
.\installer\build_installer.ps1
# Output: installer/Output/YUView-ML-Setup.exe
```

See [installer/README.md](installer/README.md) for details.

---

## Release Notes

### v2.14-ML (cumulative from 4 commits on the `develop` branch)

#### New Features

- **Floating-point RGB format support (FP16, BF16, FP32)**
  Added complete support for viewing ML pre-processed data stored in half-precision
  (IEEE 754 FP16), Brain Float (BF16), and single-precision (FP32) formats. Includes
  dedicated decode paths for each type with proper handling of subnormals, infinities,
  and NaN (mapped to zero). Supports packed (interleaved), planar, and 4th-channel-ignored
  (RGBx/BGRx) layouts for both RGB and BGR channel orders -- 18 format combinations total.

- **Per-channel scale, mean, and invert controls for float data**
  The properties panel exposes per-channel Scale, Mean, and Invert parameters that apply
  the inverse of common ML pre-processing transformations:
  `display = (raw_value / scale) + mean`. This enables direct visualization of data
  produced by pre-processing IPs that apply normalization (e.g., ImageNet mean/std).

- **Extended Custom RGB Format dialog**
  The "Custom..." format dialog now includes a Sample Type selector (UInt / Int / FP16 /
  BF16 / FP32) and an "Ignore alpha (RGBx)" checkbox. Selecting a float type automatically
  locks the bit depth. Users can construct any arbitrary float format without relying on
  presets.

- **Smart `.raw` file handling for RGB formats**
  Files with the `.raw` extension are now auto-detected as RGB when the filename contains
  float or RGB hints (e.g., `fp16`, `bf16`, `fp32`, `rgb`, `bgr`, `bgrx`). Files without
  such hints remain YUV for backward compatibility. The manual format selection dialog
  ("Raw RGB File" vs "Raw YUV File") now also works correctly for `.raw` files.

- **Windows installer (Inno Setup)**
  Added a professional Windows installer built with Inno Setup 6. Includes license page,
  optional VC++ Runtime installation, Start Menu and Desktop shortcuts, and clean
  uninstall support. Build via `installer/build_installer.ps1`.

#### Bug Fixes

- **`.raw` files could not be opened as RGB** -- The format selection logic checked
  the file extension before the explicit format parameter, so `.raw` always forced YUV
  even when the user manually selected "Raw RGB File" from the dialog. Fixed by
  restructuring the priority: explicit `fmt` parameter now takes precedence over
  extension matching.

- **Startup update popup disabled** -- The version check against the upstream YUView
  GitHub repository is now disabled unconditionally (`VERSION_CHECK = 0`). The previous
  implementation compared commit hashes with the upstream repo, which always reported
  a newer version for this custom fork.

#### Packaging & Build

- Executable renamed to **YUView-ML** to distinguish from upstream YUView.
- Added `test_windows_build.ps1` for one-command build + test + release packaging.
- Added `windows_build.md` with build environment setup instructions.
- Debug logging to `yuview_log.txt` enabled for troubleshooting.
- Automatic update checking defaults to **off** (`CHECK_FOR_UPDATES_DEFAULT = false`).

#### Tests

- Added unit tests for FP16 packed 3-channel, BF16 planar 3-channel, and FP32
  ignored-alpha format name round-trip parsing and `bytesPerFrame` validation.
- Extended conversion tests for BF16/FP16/FP32 decode accuracy across scale, mean,
  and invert parameter combinations.

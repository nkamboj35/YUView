/*  This file is part of YUView - The YUV player with advanced analytics toolset
 *   <https://github.com/IENT/YUView>
 *   Copyright (C) 2015  Institut für Nachrichtentechnik, RWTH Aachen University, GERMANY
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   In addition, as a special exception, the copyright holders give
 *   permission to link the code of portions of this program with the
 *   OpenSSL library under certain conditions as described in each
 *   individual source file, and distribute linked combinations including
 *   the two.
 *
 *   You must obey the GNU General Public License in all respects for all
 *   of the code used other than OpenSSL. If you modify file(s) with this
 *   exception, you may extend this exception to your version of the
 *   file(s), but you are not obligated to do so. If you do not wish to do
 *   so, delete this exception statement from your version. If you delete
 *   this exception statement from all source files in the program, then
 *   also delete it here.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ConversionRGB.h"

#include <video/LimitedRangeToFullRange.h>

#include <common/Functions.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace video::rgb
{

namespace
{

template <int bitDepth>
using UintValueType =
  typename std::conditional_t<bitDepth == 8,
                              uint8_t *,
                              std::conditional_t<bitDepth == 16, uint16_t *, uint32_t *>>;

template <int bitDepth, typename T> T swapBytesEndianess(const T &val)
{
  if (bitDepth <= 8)
    return val;
  if (bitDepth <= 16)
    return ((val & 0xff) << 8) | ((val & 0xff00) >> 8);
  if (bitDepth <= 32)
    return ((val & 0xff) << 24) | ((val & 0xff00) << 8) | ((val & 0xff0000) >> 8) |
           ((val & 0xff000000) >> 24);
};

int getOffsetToFirstByteOfComponent(const Channel         channel,
                                    const PixelFormatRGB &pixelFormat,
                                    const Size            frameSize)
{
  auto offset = pixelFormat.getChannelPosition(channel);
  if (pixelFormat.getDataLayout() == DataLayout::Planar)
    offset *= frameSize.width * frameSize.height;
  return offset;
}

double decodeBFloat16(uint16_t value)
{
  const auto raw = static_cast<uint32_t>(value) << 16;
  float      result;
  std::memcpy(&result, &raw, sizeof(result));
  if (!std::isfinite(result))
    return 0.0;
  return static_cast<double>(result);
}

double decodeFloat16(uint16_t value)
{
  uint16_t sign = (value >> 15) & 0x1;
  uint16_t exp = (value >> 10) & 0x1f;
  uint16_t mant = value & 0x3ff;

  if (exp == 0)
  {
    if (mant == 0)
    {
      return sign ? -0.0 : 0.0;
    }
    else
    {
      // Subnormal
      return (sign ? -1.0 : 1.0) * std::ldexp((double)mant, -24);
    }
  }
  else if (exp == 31)
  {
    // Infinity or NaN -> return 0.0 to match other decoders
    return 0.0;
  }
  else
  {
    // Normalized
    return (sign ? -1.0 : 1.0) * std::ldexp((double)(mant + 1024), exp - 25);
  }
}

double decodeFloat32(uint32_t value)
{
  float result;
  std::memcpy(&result, &value, sizeof(result));
  if (!std::isfinite(result))
    return 0.0;
  return static_cast<double>(result);
}

int finalizeFloatSample(double value, double scale, double mean, bool invert)
{
  const auto effectiveScale = std::abs(scale) < std::numeric_limits<double>::epsilon() ? 1.0 : scale;
  const auto adjusted       = (value / effectiveScale) + mean;
  const auto sanitized      = std::isfinite(adjusted) ? adjusted : 0.0;
  const auto clipped        = functions::clip(sanitized, 0.0, 255.0);
  auto       rounded        = static_cast<int>(std::lround(clipped));
  rounded                   = functions::clip(rounded, 0, 255);
  if (invert)
    rounded = 255 - rounded;
  return rounded;
}

double normalizeIntegerSample(double value, int rightShift)
{
  if (rightShift <= 0)
    return value;
  const auto divisor = std::ldexp(1.0, rightShift);
  return value / divisor;
}

int finalizeIntegerSample(double value, int rightShift, double scale, double mean, bool invert)
{
  const auto normalized = normalizeIntegerSample(value, rightShift);
  return finalizeFloatSample(normalized, scale, mean, invert);
}

void convertBFloat16RGBToARGB(const QByteArray     &sourceBuffer,
                              const PixelFormatRGB &srcPixelFormat,
                              unsigned char        *targetBuffer,
                              const Size            frameSize,
                              const bool            componentInvert[4],
                              const double          componentScale[4],
                              const double          componentMean[4],
                              const bool            limitedRange,
                              const bool            outputHasAlpha,
                              const bool            premultiplyAlpha)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto setAlpha    = outputHasAlpha && srcPixelFormat.hasAlpha();
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;
  const auto rawData     = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  auto       srcR        = rawData +
                    getOffsetToFirstByteOfComponent(Channel::Red, srcPixelFormat, frameSize);
  auto srcG = rawData + getOffsetToFirstByteOfComponent(Channel::Green, srcPixelFormat, frameSize);
  auto srcB = rawData + getOffsetToFirstByteOfComponent(Channel::Blue, srcPixelFormat, frameSize);

  const auto readValue = [&](const uint16_t *source) {
    auto value = source[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeBFloat16(value);
  };

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);

  const uint16_t *srcA = nullptr;
  if (setAlpha)
  {
    auto offsetA = srcPixelFormat.getChannelPosition(Channel::Alpha);
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      offsetA *= frameSize.width * frameSize.height;
    srcA = rawData + offsetA;
  }

  for (unsigned idx = 0; idx < pixelCount; ++idx)
  {
    auto valR = finalizeFloatSample(readValue(srcR), componentScale[0], componentMean[0], componentInvert[0]);
    auto valG = finalizeFloatSample(readValue(srcG), componentScale[1], componentMean[1], componentInvert[1]);
    auto valB = finalizeFloatSample(readValue(srcB), componentScale[2], componentMean[2], componentInvert[2]);

    if (limitedRange)
    {
      valR = LimitedRangeToFullRange.at(valR);
      valG = LimitedRangeToFullRange.at(valG);
      valB = LimitedRangeToFullRange.at(valB);
    }

    auto valA = 255;
    if (setAlpha)
    {
      valA = finalizeFloatSample(readValue(srcA), componentScale[3], componentMean[3], componentInvert[3]);
      srcA += offsetToNextValue;

      if (premultiplyAlpha)
      {
        valR = ((valR * 255) * valA) / (255 * 255);
        valG = ((valG * 255) * valA) / (255 * 255);
        valB = ((valB * 255) * valA) / (255 * 255);
      }
    }

    srcR += offsetToNextValue;
    srcG += offsetToNextValue;
    srcB += offsetToNextValue;

    targetBuffer[0] = static_cast<unsigned char>(functions::clip(valB, 0, 255));
    targetBuffer[1] = static_cast<unsigned char>(functions::clip(valG, 0, 255));
    targetBuffer[2] = static_cast<unsigned char>(functions::clip(valR, 0, 255));
    targetBuffer[3] = static_cast<unsigned char>(functions::clip(valA, 0, 255));

    targetBuffer += 4;
  }
}

void convertFloat16RGBToARGB(const QByteArray     &sourceBuffer,
                              const PixelFormatRGB &srcPixelFormat,
                              unsigned char        *targetBuffer,
                              const Size            frameSize,
                              const bool            componentInvert[4],
                              const double          componentScale[4],
                              const double          componentMean[4],
                              const bool            limitedRange,
                              const bool            outputHasAlpha,
                              const bool            premultiplyAlpha)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto setAlpha    = outputHasAlpha && srcPixelFormat.hasAlpha();
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;
  const auto rawData     = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  auto       srcR        = rawData +
                    getOffsetToFirstByteOfComponent(Channel::Red, srcPixelFormat, frameSize);
  auto srcG = rawData + getOffsetToFirstByteOfComponent(Channel::Green, srcPixelFormat, frameSize);
  auto srcB = rawData + getOffsetToFirstByteOfComponent(Channel::Blue, srcPixelFormat, frameSize);

  const auto readValue = [&](const uint16_t *source) {
    auto value = source[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeFloat16(value);
  };

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);

  const uint16_t *srcA = nullptr;
  if (setAlpha)
  {
    auto offsetA = srcPixelFormat.getChannelPosition(Channel::Alpha);
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      offsetA *= frameSize.width * frameSize.height;
    srcA = rawData + offsetA;
  }

  for (unsigned idx = 0; idx < pixelCount; ++idx)
  {
    auto valR = finalizeFloatSample(readValue(srcR), componentScale[0], componentMean[0], componentInvert[0]);
    auto valG = finalizeFloatSample(readValue(srcG), componentScale[1], componentMean[1], componentInvert[1]);
    auto valB = finalizeFloatSample(readValue(srcB), componentScale[2], componentMean[2], componentInvert[2]);

    if (limitedRange)
    {
      valR = LimitedRangeToFullRange.at(valR);
      valG = LimitedRangeToFullRange.at(valG);
      valB = LimitedRangeToFullRange.at(valB);
    }

    auto valA = 255;
    if (setAlpha)
    {
      valA = finalizeFloatSample(readValue(srcA), componentScale[3], componentMean[3], componentInvert[3]);
      srcA += offsetToNextValue;

      if (premultiplyAlpha)
      {
        valR = ((valR * 255) * valA) / (255 * 255);
        valG = ((valG * 255) * valA) / (255 * 255);
        valB = ((valB * 255) * valA) / (255 * 255);
      }
    }

    srcR += offsetToNextValue;
    srcG += offsetToNextValue;
    srcB += offsetToNextValue;

    targetBuffer[0] = static_cast<unsigned char>(functions::clip(valB, 0, 255));
    targetBuffer[1] = static_cast<unsigned char>(functions::clip(valG, 0, 255));
    targetBuffer[2] = static_cast<unsigned char>(functions::clip(valR, 0, 255));
    targetBuffer[3] = static_cast<unsigned char>(functions::clip(valA, 0, 255));

    targetBuffer += 4;
  }
}

void convertFloat32RGBToARGB(const QByteArray     &sourceBuffer,
                             const PixelFormatRGB &srcPixelFormat,
                             unsigned char        *targetBuffer,
                             const Size            frameSize,
                             const bool            componentInvert[4],
                             const double          componentScale[4],
                             const double          componentMean[4],
                             const bool            limitedRange,
                             const bool            outputHasAlpha,
                             const bool            premultiplyAlpha)
{
  const auto offsetToNextSample =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto strideBytes        = static_cast<std::ptrdiff_t>(offsetToNextSample * sizeof(float));

  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;
  const auto rawData     = reinterpret_cast<const unsigned char *>(sourceBuffer.constData());

  const auto componentOffset = [&](Channel channel) {
    const auto offset = getOffsetToFirstByteOfComponent(channel, srcPixelFormat, frameSize);
    return offset >= 0 ? static_cast<std::ptrdiff_t>(offset * sizeof(float)) : static_cast<std::ptrdiff_t>(-1);
  };

  const auto offsetR = componentOffset(Channel::Red);
  const auto offsetG = componentOffset(Channel::Green);
  const auto offsetB = componentOffset(Channel::Blue);
  if (offsetR < 0 || offsetG < 0 || offsetB < 0)
    throw std::invalid_argument("RGB channel missing in pixel format");

  auto srcR = rawData + offsetR;
  auto srcG = rawData + offsetG;
  auto srcB = rawData + offsetB;

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);

  const auto readValue = [&](const unsigned char *source) {
    uint32_t raw;
    std::memcpy(&raw, source, sizeof(raw));
    if (isBigEndian)
      raw = swapBytesEndianess<32>(raw);
    return decodeFloat32(raw);
  };

  const auto setAlpha = outputHasAlpha && srcPixelFormat.hasAlpha();
  const auto alphaOffset = componentOffset(Channel::Alpha);
  const unsigned char *srcA = nullptr;
  if (setAlpha && alphaOffset >= 0)
    srcA = rawData + alphaOffset;

  for (unsigned i = 0; i < pixelCount; ++i)
  {
    auto valR = finalizeFloatSample(readValue(srcR), componentScale[0], componentMean[0], componentInvert[0]);
    auto valG = finalizeFloatSample(readValue(srcG), componentScale[1], componentMean[1], componentInvert[1]);
    auto valB = finalizeFloatSample(readValue(srcB), componentScale[2], componentMean[2], componentInvert[2]);

    if (limitedRange)
    {
      valR = LimitedRangeToFullRange.at(valR);
      valG = LimitedRangeToFullRange.at(valG);
      valB = LimitedRangeToFullRange.at(valB);
    }

    auto valA = 255;
    if (setAlpha && srcA != nullptr)
    {
      valA = finalizeFloatSample(readValue(srcA), componentScale[3], componentMean[3], componentInvert[3]);
      srcA += strideBytes;

      if (premultiplyAlpha)
      {
        valR = ((valR * 255) * valA) / (255 * 255);
        valG = ((valG * 255) * valA) / (255 * 255);
        valB = ((valB * 255) * valA) / (255 * 255);
      }
    }

    srcR += strideBytes;
    srcG += strideBytes;
    srcB += strideBytes;

    targetBuffer[0] = static_cast<unsigned char>(functions::clip(valB, 0, 255));
    targetBuffer[1] = static_cast<unsigned char>(functions::clip(valG, 0, 255));
    targetBuffer[2] = static_cast<unsigned char>(functions::clip(valR, 0, 255));
    targetBuffer[3] = static_cast<unsigned char>(functions::clip(valA, 0, 255));

    targetBuffer += 4;
  }
}

void convertBFloat16RGBPlaneToARGB(const QByteArray     &sourceBuffer,
                                   const PixelFormatRGB &srcPixelFormat,
                                   unsigned char        *targetBuffer,
                                   const Size            frameSize,
                                   const Channel         displayChannel,
                                   const double          scale,
                                   const double          mean,
                                   const bool            invert,
                                   const bool            limitedRange)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto rawData     = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;

  auto src = rawData;
  if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
  {
    auto offset = srcPixelFormat.getChannelPosition(displayChannel);
    offset *= frameSize.width * frameSize.height;
    src += offset;
  }
  else
  {
    src += srcPixelFormat.getChannelPosition(displayChannel);
  }

  const auto readValue = [&](const uint16_t *source) {
    auto value = source[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeBFloat16(value);
  };

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);
  for (unsigned idx = 0; idx < pixelCount; ++idx)
  {
    auto val = finalizeFloatSample(readValue(src), scale, mean, invert);
    if (limitedRange)
      val = LimitedRangeToFullRange.at(val);

    targetBuffer[0] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[1] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[2] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[3] = 255;

    src += offsetToNextValue;
    targetBuffer += 4;
  }
}

void convertFloat16RGBPlaneToARGB(const QByteArray     &sourceBuffer,
                                   const PixelFormatRGB &srcPixelFormat,
                                   unsigned char        *targetBuffer,
                                   const Size            frameSize,
                                   const Channel         displayChannel,
                                   const double          scale,
                                   const double          mean,
                                   const bool            invert,
                                   const bool            limitedRange)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto rawData     = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;

  auto src = rawData;
  if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
  {
    auto offset = srcPixelFormat.getChannelPosition(displayChannel);
    offset *= frameSize.width * frameSize.height;
    src += offset;
  }
  else
  {
    src += srcPixelFormat.getChannelPosition(displayChannel);
  }

  const auto readValue = [&](const uint16_t *source) {
    auto value = source[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeFloat16(value);
  };

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);
  for (unsigned idx = 0; idx < pixelCount; ++idx)
  {
    auto val = finalizeFloatSample(readValue(src), scale, mean, invert);
    if (limitedRange)
      val = LimitedRangeToFullRange.at(val);

    targetBuffer[0] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[1] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[2] = static_cast<unsigned char>(functions::clip(val, 0, 255));
    targetBuffer[3] = 255;

    src += offsetToNextValue;
    targetBuffer += 4;
  }
}

void convertFloat32RGBPlaneToARGB(const QByteArray     &sourceBuffer,
                                  const PixelFormatRGB &srcPixelFormat,
                                  unsigned char        *targetBuffer,
                                  const Size            frameSize,
                                  const Channel         displayChannel,
                                  const double          scale,
                                  const double          mean,
                                  const bool            invert,
                                  const bool            limitedRange)
{
  const auto offsetToNextSample =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto strideBytes        = static_cast<std::ptrdiff_t>(offsetToNextSample * sizeof(float));

  const auto componentOffset = getOffsetToFirstByteOfComponent(displayChannel, srcPixelFormat, frameSize);
  if (componentOffset < 0)
    throw std::invalid_argument("Display channel missing in pixel format");

  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;

  const auto rawData = reinterpret_cast<const unsigned char *>(sourceBuffer.constData());
  const auto src     = rawData + static_cast<std::ptrdiff_t>(componentOffset * sizeof(float));

  const auto readValue = [&](const unsigned char *source) {
    uint32_t raw;
    std::memcpy(&raw, source, sizeof(raw));
    if (isBigEndian)
      raw = swapBytesEndianess<32>(raw);
    return decodeFloat32(raw);
  };

  const auto pixelCount = static_cast<unsigned>(frameSize.width * frameSize.height);

  auto current = src;
  for (unsigned idx = 0; idx < pixelCount; ++idx)
  {
    auto val = finalizeFloatSample(readValue(current), scale, mean, invert);
    if (limitedRange)
      val = LimitedRangeToFullRange.at(val);

    val = functions::clip(val, 0, 255);

    targetBuffer[0] = static_cast<unsigned char>(val);
    targetBuffer[1] = static_cast<unsigned char>(val);
    targetBuffer[2] = static_cast<unsigned char>(val);
    targetBuffer[3] = 255;

    current += strideBytes;
    targetBuffer += 4;
  }
}

rgba_t getPixelValueBFloat16(const QByteArray     &sourceBuffer,
                             const PixelFormatRGB &srcPixelFormat,
                             const Size            frameSize,
                             const QPoint         &pixelPos)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto offsetPixelPos = frameSize.width * pixelPos.y() + pixelPos.x();

  const auto rawData    = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  auto       srcPixel   = rawData + offsetPixelPos * offsetToNextValue;
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;

  auto readValue = [&](const uint16_t *src) {
    auto value = src[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeBFloat16(value);
  };

  rgba_t value{};
  for (auto channel : {Channel::Red, Channel::Green, Channel::Blue, Channel::Alpha})
  {
    if (channel == Channel::Alpha && !srcPixelFormat.hasAlpha())
      continue;

    const auto offset = getOffsetToFirstByteOfComponent(channel, srcPixelFormat, frameSize);
    auto       src    = srcPixel + offset;
    auto       val    = readValue(src);
    value[channel]   = static_cast<unsigned>(functions::clip(std::lround(val), 0l, 0xffffl));
  }

  return value;
}

rgba_t getPixelValueFloat16(const QByteArray     &sourceBuffer,
                             const PixelFormatRGB &srcPixelFormat,
                             const Size            frameSize,
                             const QPoint         &pixelPos)
{
  const auto offsetToNextValue =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto offsetPixelPos = frameSize.width * pixelPos.y() + pixelPos.x();

  const auto rawData    = reinterpret_cast<const uint16_t *>(sourceBuffer.constData());
  auto       srcPixel   = rawData + offsetPixelPos * offsetToNextValue;
  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;

  auto readValue = [&](const uint16_t *src) {
    auto value = src[0];
    if (isBigEndian)
      value = swapBytesEndianess<16>(value);
    return decodeFloat16(value);
  };

  rgba_t value{};
  for (auto channel : {Channel::Red, Channel::Green, Channel::Blue, Channel::Alpha})
  {
    if (channel == Channel::Alpha && !srcPixelFormat.hasAlpha())
      continue;

    const auto offset = getOffsetToFirstByteOfComponent(channel, srcPixelFormat, frameSize);
    auto       src    = srcPixel + offset;
    auto       val    = readValue(src);
    value[channel]   = static_cast<unsigned>(functions::clip(std::lround(val), 0l, 0xffffl));
  }

  return value;
}

rgba_t getPixelValueFloat32(const QByteArray     &sourceBuffer,
                            const PixelFormatRGB &srcPixelFormat,
                            const Size            frameSize,
                            const QPoint         &pixelPos)
{
  const auto offsetToNextSample =
      srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto strideBytes = static_cast<std::ptrdiff_t>(offsetToNextSample * sizeof(float));
  const auto pixelIndex  = frameSize.width * pixelPos.y() + pixelPos.x();

  const auto rawData = reinterpret_cast<const unsigned char *>(sourceBuffer.constData());
  auto srcPixel = rawData + static_cast<std::ptrdiff_t>(pixelIndex * strideBytes);

  const auto isBigEndian = srcPixelFormat.getEndianess() == Endianness::Big;
  const auto readValue   = [&](const unsigned char *source) {
    uint32_t raw;
    std::memcpy(&raw, source, sizeof(raw));
    if (isBigEndian)
      raw = swapBytesEndianess<32>(raw);
    return decodeFloat32(raw);
  };

  rgba_t value{};
  for (auto channel : {Channel::Red, Channel::Green, Channel::Blue, Channel::Alpha})
  {
    if (channel == Channel::Alpha && !srcPixelFormat.hasAlpha())
      continue;

    const auto offset = getOffsetToFirstByteOfComponent(channel, srcPixelFormat, frameSize);
    auto       src    = srcPixel + static_cast<std::ptrdiff_t>(offset * sizeof(float));
    auto       val    = readValue(src);
    value[channel]   = static_cast<unsigned>(functions::clip(std::lround(val), 0l, 0xffffl));
  }

  return value;
}

// Convert the input format to the output RGBA format. Apply inversion, scaling,
// limited range conversion and alpha multiplication. The input can be any supported
// format. The output is always 8 bit ARGB little endian.
template <int bitDepth>
void convertRGBToARGB(const QByteArray     &sourceBuffer,
                      const PixelFormatRGB &srcPixelFormat,
                      unsigned char        *targetBuffer,
                      const Size            frameSize,
                      const bool            componentInvert[4],
                      const double          componentScale[4],
                      const double          componentMean[4],
                      const bool            limitedRange,
                      const bool            outputHasAlpha,
                      const bool            premultiplyAlpha)
{
  const int  rightShift = bitDepth == 8 ? 0 : (srcPixelFormat.getBitsPerSample() - 8);
  const auto offsetToNextValue =
    srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();

  using InValueType   = UintValueType<bitDepth>;
  const auto setAlpha = outputHasAlpha && srcPixelFormat.hasAlpha();

  const auto rawData = (InValueType)sourceBuffer.data();

  auto srcR = rawData + getOffsetToFirstByteOfComponent(Channel::Red, srcPixelFormat, frameSize);
  auto srcG = rawData + getOffsetToFirstByteOfComponent(Channel::Green, srcPixelFormat, frameSize);
  auto srcB = rawData + getOffsetToFirstByteOfComponent(Channel::Blue, srcPixelFormat, frameSize);

  InValueType srcA = nullptr;
  if (setAlpha)
  {
    auto offsetA = srcPixelFormat.getChannelPosition(Channel::Alpha);
    if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
      offsetA *= frameSize.width * frameSize.height;
    srcA = ((InValueType)sourceBuffer.data()) + offsetA;
  }

  for (unsigned i = 0; i < frameSize.width * frameSize.height; i++)
  {
    const auto isBigEndian = bitDepth > 8 && srcPixelFormat.getEndianess() == Endianness::Big;
    auto convertValue = [&](const InValueType sourceData,
                            const double     scale,
                            const double     mean,
                            const bool       invert) {
      auto value = static_cast<int64_t>(sourceData[0]);
      if (isBigEndian)
        value = swapBytesEndianess<bitDepth>(value);
      return finalizeIntegerSample(static_cast<double>(value), rightShift, scale, mean, invert);
    };

    auto valR = convertValue(srcR, componentScale[0], componentMean[0], componentInvert[0]);
    auto valG = convertValue(srcG, componentScale[1], componentMean[1], componentInvert[1]);
    auto valB = convertValue(srcB, componentScale[2], componentMean[2], componentInvert[2]);

    if (limitedRange)
    {
      valR = LimitedRangeToFullRange.at(valR);
      valG = LimitedRangeToFullRange.at(valG);
      valB = LimitedRangeToFullRange.at(valB);
      // No limited range for alpha
    }

    int valA = 255;
    if (setAlpha)
    {
      valA = convertValue(srcA, componentScale[3], componentMean[3], componentInvert[3]);
      srcA += offsetToNextValue;

      if (premultiplyAlpha)
      {
        valR = ((valR * 255) * valA) / (255 * 255);
        valG = ((valG * 255) * valA) / (255 * 255);
        valB = ((valB * 255) * valA) / (255 * 255);
      }
    }

    srcR += offsetToNextValue;
    srcG += offsetToNextValue;
    srcB += offsetToNextValue;

    targetBuffer[0] = valB;
    targetBuffer[1] = valG;
    targetBuffer[2] = valR;
    targetBuffer[3] = valA;

    targetBuffer += 4;
  }
}

// Convert one single plane of the input format to RGBA. This is used to visualize the individual
// components.
template <int bitDepth>
void convertRGBPlaneToARGB(const QByteArray     &sourceBuffer,
                           const PixelFormatRGB &srcPixelFormat,
                           unsigned char        *targetBuffer,
                           const Size            frameSize,
                           const Channel         displayChannel,
                           const double          scale,
                           const double          mean,
                           const bool            invert,
                           const bool            limitedRange)
{
  const auto shiftTo8Bit = srcPixelFormat.getBitsPerSample() - 8;
  const auto offsetToNextValue =
    srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();

  using InValueType = UintValueType<bitDepth>;

  auto       src                    = (InValueType)sourceBuffer.data();
  const auto displayComponentOffset = srcPixelFormat.getChannelPosition(displayChannel);
  if (srcPixelFormat.getDataLayout() == DataLayout::Planar)
    src += displayComponentOffset * frameSize.width * frameSize.height;
  else
    src += displayComponentOffset;

  for (size_t i = 0; i < frameSize.width * frameSize.height; i++)
  {
    auto value = static_cast<int64_t>(src[0]);
    if (bitDepth > 8 && srcPixelFormat.getEndianess() == Endianness::Big)
      value = swapBytesEndianess<bitDepth>(value);
    auto val = finalizeIntegerSample(static_cast<double>(value), shiftTo8Bit, scale, mean, invert);
    if (limitedRange)
      val = LimitedRangeToFullRange.at(val);

    targetBuffer[0] = val;
    targetBuffer[1] = val;
    targetBuffer[2] = val;
    targetBuffer[3] = 255;

    src += offsetToNextValue;
    targetBuffer += 4;
  }
}

template <int bitDepth>
rgba_t getPixelValue(const QByteArray     &sourceBuffer,
                     const PixelFormatRGB &srcPixelFormat,
                     const Size            frameSize,
                     const QPoint         &pixelPos)
{
  const auto offsetToNextValue =
    srcPixelFormat.getDataLayout() == DataLayout::Planar ? 1 : srcPixelFormat.nrChannels();
  const auto offsetPixelPos = frameSize.width * pixelPos.y() + pixelPos.x();

  using InValueType = UintValueType<bitDepth>;

  const auto rawData  = (InValueType)sourceBuffer.data();
  auto       srcPixel = rawData + offsetPixelPos * offsetToNextValue;

  rgba_t value{};
  for (auto channel : {Channel::Red, Channel::Green, Channel::Blue, Channel::Alpha})
  {
    if (channel == Channel::Alpha && !srcPixelFormat.hasAlpha())
      continue;

    const auto offset = getOffsetToFirstByteOfComponent(channel, srcPixelFormat, frameSize);

    auto src = srcPixel + offset;
    auto val = (unsigned)src[0];
    if (bitDepth > 8 && srcPixelFormat.getEndianess() == Endianness::Big)
      val = swapBytesEndianess<bitDepth>(val);
    value[channel] = val;
  }

  return value;
}

} // namespace

void convertInputRGBToARGB(const QByteArray     &sourceBuffer,
                           const PixelFormatRGB &srcPixelFormat,
                           unsigned char        *targetBuffer,
                           const Size            frameSize,
                           const bool            componentInvert[4],
                           const double          componentScale[4],
                           const double          componentMean[4],
                           const bool            limitedRange,
                           const bool            outputHasAlpha,
                           const bool            premultiplyAlpha)
{
  const auto sampleType = srcPixelFormat.getSampleType();
  if (sampleType == SampleType::BFloat16)
  {
    convertBFloat16RGBToARGB(sourceBuffer,
                             srcPixelFormat,
                             targetBuffer,
                             frameSize,
                             componentInvert,
                             componentScale,
                             componentMean,
                             limitedRange,
                             outputHasAlpha,
                             premultiplyAlpha);
    return;
  }
  if (sampleType == SampleType::Float16)
  {
    convertFloat16RGBToARGB(sourceBuffer,
                             srcPixelFormat,
                             targetBuffer,
                             frameSize,
                             componentInvert,
                             componentScale,
                             componentMean,
                             limitedRange,
                             outputHasAlpha,
                             premultiplyAlpha);
    return;
  }
  if (sampleType == SampleType::Float32)
  {
    convertFloat32RGBToARGB(sourceBuffer,
                             srcPixelFormat,
                             targetBuffer,
                             frameSize,
                             componentInvert,
                             componentScale,
                             componentMean,
                             limitedRange,
                             outputHasAlpha,
                             premultiplyAlpha);
    return;
  }
  if (sampleType != SampleType::UnsignedInteger && sampleType != SampleType::SignedInteger)
    throw std::invalid_argument("Unsupported RGB sample type for conversion");

  const auto bitsPerSample = srcPixelFormat.getBitsPerSample();
  if (bitsPerSample < 8 || bitsPerSample > 32)
    throw std::invalid_argument("Invalid bit depth in pixel format for conversion");

  if (bitsPerSample == 8)
    convertRGBToARGB<8>(sourceBuffer,
                        srcPixelFormat,
                        targetBuffer,
                        frameSize,
                        componentInvert,
                        componentScale,
                        componentMean,
                        limitedRange,
                        outputHasAlpha,
                        premultiplyAlpha);
  else if (bitsPerSample <= 16)
    convertRGBToARGB<16>(sourceBuffer,
                         srcPixelFormat,
                         targetBuffer,
                         frameSize,
                         componentInvert,
                         componentScale,
                         componentMean,
                         limitedRange,
                         outputHasAlpha,
                         premultiplyAlpha);
  else
    convertRGBToARGB<32>(sourceBuffer,
                         srcPixelFormat,
                         targetBuffer,
                         frameSize,
                         componentInvert,
                         componentScale,
                         componentMean,
                         limitedRange,
                         outputHasAlpha,
                         premultiplyAlpha);
}

void convertSinglePlaneOfRGBToGreyscaleARGB(const QByteArray     &sourceBuffer,
                                            const PixelFormatRGB &srcPixelFormat,
                                            unsigned char        *targetBuffer,
                                            const Size            frameSize,
                                            const Channel         displayChannel,
                                            const double          scale,
                                            const double          mean,
                                            const bool            invert,
                                            const bool            limitedRange)
{
  const auto sampleType = srcPixelFormat.getSampleType();
  if (sampleType == SampleType::BFloat16)
  {
    convertBFloat16RGBPlaneToARGB(sourceBuffer,
                    srcPixelFormat,
                    targetBuffer,
                    frameSize,
                    displayChannel,
                    scale,
                    mean,
                    invert,
                    limitedRange);
    return;
  }
  if (sampleType == SampleType::Float16)
  {
    convertFloat16RGBPlaneToARGB(sourceBuffer,
                    srcPixelFormat,
                    targetBuffer,
                    frameSize,
                    displayChannel,
                    scale,
                    mean,
                    invert,
                    limitedRange);
    return;
  }
  if (sampleType == SampleType::Float32)
  {
    convertFloat32RGBPlaneToARGB(sourceBuffer,
                                 srcPixelFormat,
                                 targetBuffer,
                                 frameSize,
                                 displayChannel,
                                 scale,
                                 mean,
                                 invert,
                                 limitedRange);
    return;
  }
  if (sampleType != SampleType::UnsignedInteger && sampleType != SampleType::SignedInteger)
    throw std::invalid_argument("Unsupported RGB sample type for conversion");

  const auto bitsPerSample = srcPixelFormat.getBitsPerSample();
  if (bitsPerSample < 8 || bitsPerSample > 32)
    throw std::invalid_argument("Invalid bit depth in pixel format for conversion");

  if (bitsPerSample == 8)
    convertRGBPlaneToARGB<8>(sourceBuffer,
                             srcPixelFormat,
                             targetBuffer,
                             frameSize,
                             displayChannel,
                             scale,
                             mean,
                             invert,
                             limitedRange);
  else if (bitsPerSample <= 16)
    convertRGBPlaneToARGB<16>(sourceBuffer,
                              srcPixelFormat,
                              targetBuffer,
                              frameSize,
                              displayChannel,
                              scale,
                              mean,
                              invert,
                              limitedRange);
  else
    convertRGBPlaneToARGB<32>(sourceBuffer,
                              srcPixelFormat,
                              targetBuffer,
                              frameSize,
                              displayChannel,
                              scale,
                              mean,
                              invert,
                              limitedRange);
}

rgba_t getPixelValueFromBuffer(const QByteArray     &sourceBuffer,
                               const PixelFormatRGB &srcPixelFormat,
                               const Size            frameSize,
                               const QPoint         &pixelPos)
{
  const auto sampleType = srcPixelFormat.getSampleType();
  if (sampleType == SampleType::BFloat16)
    return getPixelValueBFloat16(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
  if (sampleType == SampleType::Float16)
    return getPixelValueFloat16(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
  if (sampleType == SampleType::Float32)
    return getPixelValueFloat32(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
  if (sampleType != SampleType::UnsignedInteger && sampleType != SampleType::SignedInteger)
    throw std::invalid_argument("Unsupported RGB sample type for conversion");

  const auto bitsPerSample = srcPixelFormat.getBitsPerSample();
  if (bitsPerSample < 8 || bitsPerSample > 32)
    throw std::invalid_argument("Invalid bit depth in pixel format for conversion");

  if (bitsPerSample == 8)
    return getPixelValue<8>(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
  else if (bitsPerSample <= 16)
    return getPixelValue<16>(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
  else
    return getPixelValue<32>(sourceBuffer, srcPixelFormat, frameSize, pixelPos);
}

} // namespace video::rgb

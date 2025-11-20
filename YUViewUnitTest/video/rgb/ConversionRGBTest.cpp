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

#include <common/Testing.h>

#include <video/LimitedRangeToFullRange.h>
#include <video/rgb/ConversionRGB.h>

#include <common/functions.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "CreateTestData.h"

using OutputHasAlpha        = bool;
using PremultiplyAlpha      = bool;
using ScalingPerComponent   = std::array<double, 4>;
using MeanPerComponent      = std::array<double, 4>;
using InversionPerComponent = std::array<bool, 4>;
using UChaVector            = std::vector<unsigned char>;

namespace video::rgb::test
{

const std::vector<ScalingPerComponent> ScalingPerComponentToTest = {
  {1.0, 1.0, 1.0, 1.0},
  {2.0, 1.0, 1.0, 1.0},
  {1.0, 2.0, 1.0, 1.0},
  {1.0, 1.0, 2.0, 1.0},
  {1.0, 1.0, 1.0, 2.0},
  {1.0, 8.0, 1.0, 1.0}};

const std::vector<MeanPerComponent> MeanPerComponentToTest = {
  {0.0, 0.0, 0.0, 0.0},
  {10.0, 0.0, 0.0, 0.0}};

const std::vector<InversionPerComponent> InversionPerComponentToTest = {
  {false, false, false, false},
  {true, false, false, false},
  {false, true, false, false},
  {false, false, true, false},
  {false, false, false, true},
  {true, true, true, true}};

uint16_t floatToBFloat16(float value)
{
  uint32_t raw;
  std::memcpy(&raw, &value, sizeof(float));
  return static_cast<uint16_t>(raw >> 16);
}

int scaleShiftClipInvertValue(const int    value,
                              const int    bitDepth,
                              const double scale,
                              const double mean,
                              const bool   invert)
{
  const auto valueOriginalDepth = static_cast<double>(convertBitness(value, 12, bitDepth));
  const auto divisor            = std::ldexp(1.0, bitDepth - 8);
  const auto normalized         = valueOriginalDepth / divisor;
  const auto effectiveScale     = std::abs(scale) < std::numeric_limits<double>::epsilon() ? 1.0 : scale;
  const auto adjusted           = (normalized / effectiveScale) + mean;
  const auto clippedDouble      = functions::clip(adjusted, 0.0, 255.0);
  auto       clipped            = static_cast<int>(std::lround(clippedDouble));
  clipped                       = functions::clip(clipped, 0, 255);
  return invert ? (255 - clipped) : clipped;
};

uint32_t floatToUInt32(float value)
{
  uint32_t raw;
  std::memcpy(&raw, &value, sizeof(float));
  return raw;
}

rgba_t getARGBValueFromDataLittleEndian(const UChaVector &data, const size_t i)
{
  const auto pixelOffset = i * 4;
  return rgba_t({data.at(pixelOffset + 2),
                 data.at(pixelOffset + 1),
                 data.at(pixelOffset),
                 data.at(pixelOffset + 3)});
}

void checkOutputValues(const UChaVector            &data,
                       const int                    bitDepth,
                       const ScalingPerComponent   &scaling,
                       const MeanPerComponent      &mean,
                       const bool                   limitedRange,
                       const InversionPerComponent &inversion,
                       const bool                   alphaShouldBeSet)
{
  for (size_t i = 0; i < TEST_FRAME_NR_VALUES; ++i)
  {
    auto expectedValue = TEST_VALUES_12BIT.at(i);

    expectedValue.R = scaleShiftClipInvertValue(
      expectedValue.R, bitDepth, scaling[0], mean[0], inversion[0]);
    expectedValue.G = scaleShiftClipInvertValue(
      expectedValue.G, bitDepth, scaling[1], mean[1], inversion[1]);
    expectedValue.B = scaleShiftClipInvertValue(
      expectedValue.B, bitDepth, scaling[2], mean[2], inversion[2]);
    expectedValue.A = scaleShiftClipInvertValue(
      expectedValue.A, bitDepth, scaling[3], mean[3], inversion[3]);

    if (limitedRange)
    {
      expectedValue.R = LimitedRangeToFullRange.at(expectedValue.R);
      expectedValue.G = LimitedRangeToFullRange.at(expectedValue.G);
      expectedValue.B = LimitedRangeToFullRange.at(expectedValue.B);
      // No limited range for alpha
    }

    if (!alphaShouldBeSet)
      expectedValue.A = 255;

    const auto actualValue = getARGBValueFromDataLittleEndian(data, i);

    if (expectedValue != actualValue)
      throw std::runtime_error("Value " + std::to_string(i));
  }
}

void checkOutputValuesForPlane(const UChaVector            &data,
                               const PixelFormatRGB        &pixelFormat,
                               const ScalingPerComponent   &scaling,
                               const MeanPerComponent      &mean,
                               const bool                   limitedRange,
                               const InversionPerComponent &inversion,
                               const Channel                channel)
{
  for (size_t i = 0; i < TEST_FRAME_NR_VALUES; ++i)
  {
    auto expectedPlaneValue = TEST_VALUES_12BIT[i].at(channel);

    const auto channelIndex = ChannelMapper.indexOf(channel);
    const auto bitDepth     = pixelFormat.getBitsPerSample();

    expectedPlaneValue = scaleShiftClipInvertValue(expectedPlaneValue,
                             bitDepth,
                             scaling[channelIndex],
                             mean[channelIndex],
                             inversion[channelIndex]);

    if (limitedRange)
      expectedPlaneValue = LimitedRangeToFullRange.at(expectedPlaneValue);

    const auto expectedValue =
        rgba_t({expectedPlaneValue, expectedPlaneValue, expectedPlaneValue, 255});

    const auto actualValue = getARGBValueFromDataLittleEndian(data, i);

    if (expectedValue != actualValue)
      throw std::runtime_error("Value " + std::to_string(i));
  }
}

void testConversionToRGBA(const QByteArray            &sourceBuffer,
                          const PixelFormatRGB        &srcPixelFormat,
                          const InversionPerComponent &inversion,
                          const ScalingPerComponent   &componentScale,
                          const MeanPerComponent      &componentMean,
                          const bool                   limitedRange,
                          const bool                   outputHasAlpha)
{
  UChaVector outputBuffer;
  outputBuffer.resize(TEST_FRAME_NR_VALUES * 4);

  convertInputRGBToARGB(sourceBuffer,
                        srcPixelFormat,
                        outputBuffer.data(),
                        TEST_FRAME_SIZE,
                        inversion.data(),
                        componentScale.data(),
                        componentMean.data(),
                        limitedRange,
                        outputHasAlpha,
                        PremultiplyAlpha(false));

  const auto alphaShouldBeSet = (outputHasAlpha && srcPixelFormat.hasAlpha());
  checkOutputValues(outputBuffer,
                    srcPixelFormat.getBitsPerSample(),
                    componentScale,
                    componentMean,
                    limitedRange,
                    inversion,
                    alphaShouldBeSet);
}

void testConversionToRGBASinglePlane(const QByteArray            &sourceBuffer,
                                     const PixelFormatRGB        &srcPixelFormat,
                                     const InversionPerComponent &inversion,
                                     const ScalingPerComponent   &componentScale,
                                     const MeanPerComponent      &componentMean,
                                     const bool                   limitedRange,
                                     const bool)
{
  for (const auto channel : ChannelMapper.getValues())
  {
    if (channel == Channel::Alpha && !srcPixelFormat.hasAlpha())
      continue;

    UChaVector outputBuffer;
    outputBuffer.resize(TEST_FRAME_NR_VALUES * 4);

    const auto channelIndex = ChannelMapper.indexOf(channel);

    convertSinglePlaneOfRGBToGreyscaleARGB(sourceBuffer,
                                           srcPixelFormat,
                                           outputBuffer.data(),
                                           TEST_FRAME_SIZE,
                                           channel,
                                           componentScale[channelIndex],
                                           componentMean[channelIndex],
                                           inversion[channelIndex],
                                           limitedRange);

    checkOutputValuesForPlane(outputBuffer,
                              srcPixelFormat,
                              componentScale,
                              componentMean,
                              limitedRange,
                              inversion,
                              channel);
  }
}

using TestingFunction = std::function<void(const QByteArray &,
                                           const video::rgb::PixelFormatRGB &,
                                           const InversionPerComponent &,
                                           const ScalingPerComponent &,
                                           const MeanPerComponent &,
                                           const bool,
                                           const bool)>;

void runTestForAllParameters(TestingFunction testingFunction)
{
  for (const auto endianness : {Endianness::Little, Endianness::Big})
  {
    for (const auto bitDepth : {8, 10, 12, 16, 32})
    {
      for (const auto &alphaMode : AlphaModeMapper.getValues())
      {
        for (const auto &dataLayout : video::DataLayoutMapper.getValues())
        {
          for (const auto &channelOrder : video::rgb::ChannelOrderMapper.getValues())
          {
            const video::rgb::PixelFormatRGB format(
                bitDepth, dataLayout, channelOrder, alphaMode, endianness);
            const auto data = createRawRGBData(format);

            for (const auto outputHasAlpha : {false, true})
            {
              for (const auto &componentScale : ScalingPerComponentToTest)
              {
                for (const auto &componentMean : MeanPerComponentToTest)
                {
                  for (const auto &inversion : InversionPerComponentToTest)
                  {
                    for (const auto limitedRange : {false, true})
                    {
                      EXPECT_NO_THROW(testingFunction(data,
                                                      format,
                                                      inversion,
                                                      componentScale,
                                                      componentMean,
                                                      limitedRange,
                                                      outputHasAlpha))
                          << "parametersAsString";
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}

TEST(ConversionRGBTest, TestConversionToRGBA)
{
  runTestForAllParameters(testConversionToRGBA);
}

TEST(ConversionRGBTest, TestConversionOfSinglePlaneToRGBA)
{
  runTestForAllParameters(testConversionToRGBASinglePlane);
}

TEST(ConversionRGBTest, TestBFloat16Conversion)
{
  const PixelFormatRGB format(16,
                              DataLayout::Packed,
                              ChannelOrder::RGB,
                              AlphaMode::Last,
                              Endianness::Little,
                              SampleType::BFloat16);
  const Size frameSize{2, 1};

  QByteArray source;
  source.resize(static_cast<int>(frameSize.width * frameSize.height * format.nrChannels() * 2));
  auto raw = reinterpret_cast<uint16_t *>(source.data());

  // Pixel 0: black with full alpha
  raw[0] = floatToBFloat16(0.0f);
  raw[1] = floatToBFloat16(0.0f);
  raw[2] = floatToBFloat16(0.0f);
  raw[3] = floatToBFloat16(255.0f);

  // Pixel 1: R=255, G=128, B=64, alpha=255
  raw[4] = floatToBFloat16(255.0f);
  raw[5] = floatToBFloat16(128.0f);
  raw[6] = floatToBFloat16(64.0f);
  raw[7] = floatToBFloat16(255.0f);

  std::array<double, 4> scale{1.0, 1.0, 1.0, 1.0};
  std::array<double, 4> mean{0.0, 0.0, 0.0, 0.0};
  std::array<bool, 4>  invert{false, false, false, false};
  std::vector<uint8_t> rgba(frameSize.width * frameSize.height * 4);

  convertInputRGBToARGB(source,
                        format,
                        rgba.data(),
                        frameSize,
                        invert.data(),
                        scale.data(),
                        mean.data(),
                        false,
                        true,
                        false);

  EXPECT_EQ(rgba[0], 0u);   // B pixel 0
  EXPECT_EQ(rgba[1], 0u);   // G pixel 0
  EXPECT_EQ(rgba[2], 0u);   // R pixel 0
  EXPECT_EQ(rgba[3], 255u); // A pixel 0

  EXPECT_EQ(rgba[4], 64u);  // B pixel 1
  EXPECT_EQ(rgba[5], 128u); // G pixel 1
  EXPECT_EQ(rgba[6], 255u); // R pixel 1
  EXPECT_EQ(rgba[7], 255u); // A pixel 1

  std::vector<uint8_t> plane(frameSize.width * frameSize.height * 4);
  convertSinglePlaneOfRGBToGreyscaleARGB(source,
                                         format,
                                         plane.data(),
                                         frameSize,
                                         Channel::Green,
                                         1.0,
                                         0.0,
                                         false,
                                         false);

  EXPECT_EQ(plane[0], 0u);
  EXPECT_EQ(plane[1], 0u);
  EXPECT_EQ(plane[2], 0u);
  EXPECT_EQ(plane[3], 255u);
  EXPECT_EQ(plane[4], 128u);
  EXPECT_EQ(plane[5], 128u);
  EXPECT_EQ(plane[6], 128u);
  EXPECT_EQ(plane[7], 255u);

  const auto pixel = getPixelValueFromBuffer(source, format, frameSize, QPoint(1, 0));
  EXPECT_EQ(pixel.R, 255u);
  EXPECT_EQ(pixel.G, 128u);
  EXPECT_EQ(pixel.B, 64u);
  EXPECT_EQ(pixel.A, 255u);
}

TEST(ConversionRGBTest, TestBFloat16BGRXConversion)
{
  const PixelFormatRGB format(16,
                              DataLayout::Packed,
                              ChannelOrder::BGR,
                              AlphaMode::Last,
                              Endianness::Little,
                              SampleType::BFloat16,
                              true);
  const Size frameSize{2, 1};

  QByteArray source;
  source.resize(static_cast<int>(frameSize.width * frameSize.height * format.nrChannels() * 2));
  auto raw = reinterpret_cast<uint16_t *>(source.data());

  // Pixel 0: black, unused alpha slot ignored
  raw[0] = floatToBFloat16(0.0f);  // B
  raw[1] = floatToBFloat16(0.0f);  // G
  raw[2] = floatToBFloat16(0.0f);  // R
  raw[3] = floatToBFloat16(0.0f);  // X

  // Pixel 1: B=64, G=128, R=255, alpha ignored
  raw[4] = floatToBFloat16(64.0f);
  raw[5] = floatToBFloat16(128.0f);
  raw[6] = floatToBFloat16(255.0f);
  raw[7] = floatToBFloat16(42.0f);

  std::array<double, 4> scale{1.0, 1.0, 1.0, 1.0};
  std::array<double, 4> mean{0.0, 0.0, 0.0, 0.0};
  std::array<bool, 4>  invert{false, false, false, false};
  std::vector<uint8_t> rgba(frameSize.width * frameSize.height * 4);

  convertInputRGBToARGB(source,
                        format,
                        rgba.data(),
                        frameSize,
                        invert.data(),
                        scale.data(),
                        mean.data(),
                        false,
                        true,
                        false);

  EXPECT_EQ(rgba[0], 0u);
  EXPECT_EQ(rgba[1], 0u);
  EXPECT_EQ(rgba[2], 0u);
  EXPECT_EQ(rgba[3], 255u);

  EXPECT_EQ(rgba[4], 64u);
  EXPECT_EQ(rgba[5], 128u);
  EXPECT_EQ(rgba[6], 255u);
  EXPECT_EQ(rgba[7], 255u);

  const auto pixel = getPixelValueFromBuffer(source, format, frameSize, QPoint(1, 0));
  EXPECT_EQ(pixel.R, 255u);
  EXPECT_EQ(pixel.G, 128u);
  EXPECT_EQ(pixel.B, 64u);
  EXPECT_EQ(pixel.A, 0u);
}

TEST(ConversionRGBTest, TestFloat32Conversion)
{
  const Size frameSize{2, 1};

  const PixelFormatRGB packedFormat(32,
                                    DataLayout::Packed,
                                    ChannelOrder::RGB,
                                    AlphaMode::None,
                                    Endianness::Little,
                                    SampleType::Float32);

  QByteArray packedSource;
  packedSource.resize(static_cast<int>(frameSize.width * frameSize.height * packedFormat.nrChannels() * sizeof(float)));
  auto packedRaw = reinterpret_cast<uint32_t *>(packedSource.data());

  // Pixel 0
  packedRaw[0] = floatToUInt32(0.0f);
  packedRaw[1] = floatToUInt32(0.0f);
  packedRaw[2] = floatToUInt32(0.0f);
  // Pixel 1
  packedRaw[3] = floatToUInt32(255.0f);
  packedRaw[4] = floatToUInt32(128.0f);
  packedRaw[5] = floatToUInt32(64.0f);

  std::array<double, 4> scale{1.0, 1.0, 1.0, 1.0};
  std::array<double, 4> mean{0.0, 0.0, 0.0, 0.0};
  std::array<bool, 4>  invert{false, false, false, false};
  std::vector<uint8_t> rgba(frameSize.width * frameSize.height * 4);

  convertInputRGBToARGB(packedSource,
                        packedFormat,
                        rgba.data(),
                        frameSize,
                        invert.data(),
                        scale.data(),
                        mean.data(),
                        false,
                        true,
                        false);

  EXPECT_EQ(rgba[0], 0u);
  EXPECT_EQ(rgba[1], 0u);
  EXPECT_EQ(rgba[2], 0u);
  EXPECT_EQ(rgba[3], 255u);

  EXPECT_EQ(rgba[4], 64u);
  EXPECT_EQ(rgba[5], 128u);
  EXPECT_EQ(rgba[6], 255u);
  EXPECT_EQ(rgba[7], 255u);

  const PixelFormatRGB planarFormat(32,
                                    DataLayout::Planar,
                                    ChannelOrder::RGB,
                                    AlphaMode::None,
                                    Endianness::Little,
                                    SampleType::Float32);

  QByteArray planarSource;
  planarSource.resize(static_cast<int>(frameSize.width * frameSize.height * planarFormat.nrChannels() * sizeof(float)));
  auto planarRaw = reinterpret_cast<uint32_t *>(planarSource.data());

  // Plane R
  planarRaw[0] = floatToUInt32(0.0f);
  planarRaw[1] = floatToUInt32(255.0f);
  // Plane G
  planarRaw[2] = floatToUInt32(0.0f);
  planarRaw[3] = floatToUInt32(128.0f);
  // Plane B
  planarRaw[4] = floatToUInt32(0.0f);
  planarRaw[5] = floatToUInt32(64.0f);

  std::vector<uint8_t> planarRgba(frameSize.width * frameSize.height * 4);
  convertInputRGBToARGB(planarSource,
                        planarFormat,
                        planarRgba.data(),
                        frameSize,
                        invert.data(),
                        scale.data(),
                        mean.data(),
                        false,
                        true,
                        false);

  EXPECT_EQ(planarRgba[4], 64u);
  EXPECT_EQ(planarRgba[5], 128u);
  EXPECT_EQ(planarRgba[6], 255u);
  EXPECT_EQ(planarRgba[7], 255u);

  std::vector<uint8_t> plane(frameSize.width * frameSize.height * 4);
  convertSinglePlaneOfRGBToGreyscaleARGB(planarSource,
                                         planarFormat,
                                         plane.data(),
                                         frameSize,
                                         Channel::Green,
                                         1.0,
                                         0.0,
                                         false,
                                         false);

  EXPECT_EQ(plane[4], 128u);
  EXPECT_EQ(plane[5], 128u);
  EXPECT_EQ(plane[6], 128u);
  EXPECT_EQ(plane[7], 255u);

  const auto pixel = getPixelValueFromBuffer(planarSource, planarFormat, frameSize, QPoint(1, 0));
  EXPECT_EQ(pixel.R, 255u);
  EXPECT_EQ(pixel.G, 128u);
  EXPECT_EQ(pixel.B, 64u);
  EXPECT_EQ(pixel.A, 0u);
}

} // namespace video::rgb::test

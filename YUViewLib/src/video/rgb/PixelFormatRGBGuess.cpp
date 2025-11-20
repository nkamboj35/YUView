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

#include "PixelFormatRGBGuess.h"

#include <common/Functions.h>

#include <regex>
#include <string>

using namespace std::string_literals;
using filesource::frameFormatGuess::FileInfoForGuess;
using filesource::frameFormatGuess::GuessedFrameFormat;

namespace video::rgb
{

namespace
{

const auto DEFAULT_PIXEL_FORMAT = PixelFormatRGB(8, DataLayout::Packed, ChannelOrder::RGB);

void applySampleTypeFromTokens(PixelFormatRGB &format, const std::string &name)
{
  const auto nameLower = functions::toLower(name);

  if (nameLower.find("bf16") != std::string::npos)
  {
    format.setSampleType(SampleType::BFloat16);
    format.setBitsPerSample(16);
  }
  else if (nameLower.find("fp16") != std::string::npos)
  {
    format.setSampleType(SampleType::Float16);
    format.setBitsPerSample(16);
  }
  else if (nameLower.find("fp32") != std::string::npos)
  {
    format.setSampleType(SampleType::Float32);
    format.setBitsPerSample(32);
  }
}

DataLayout findDataLayoutInName(const std::string &fileName)
{
  std::string matcher = "(?:_|\\.|-)(packed|planar)(?:_|\\.|-)";

  std::smatch sm;
  std::regex  strExpr(matcher);
  if (std::regex_search(fileName, sm, strExpr))
  {
    auto match = sm.str(0).substr(1, 6);
    return match == "planar" ? DataLayout::Planar : DataLayout::Packed;
  }

  for (const auto &[order, name] : ChannelOrderMapper)
  {
    const auto lowerOrder = functions::toLower(name);
    if (fileName.find(lowerOrder + "p") != std::string::npos)
      return DataLayout::Planar;
    if (fileName.find("a" + lowerOrder + "p") != std::string::npos)
      return DataLayout::Planar;
    if (fileName.find(lowerOrder + "ap") != std::string::npos)
      return DataLayout::Planar;
    if (fileName.find(lowerOrder + "xp") != std::string::npos)
      return DataLayout::Planar;
  }

  return DataLayout::Packed;
}

bool doesPixelFormatMatchFileSize(const PixelFormatRGB         &pixelFormat,
                                  const Size                   &frameSize,
                                  const std::optional<int64_t> &fileSize)
{
  if (!fileSize)
    return true;

  const auto bytesPerFrame = pixelFormat.bytesPerFrame(frameSize);
  if (bytesPerFrame <= 0)
    return false;

  const auto isFileSizeAMultipleOfFrameSize = (*fileSize % bytesPerFrame) == 0;
  return isFileSizeAMultipleOfFrameSize;
}

} // namespace

std::optional<PixelFormatRGB> checkForPixelFormatIndicatorInName(
    const std::string &filename, const Size &frameSize, const std::optional<std::int64_t> &fileSize)
{
  std::string matcher = "(?:_|\\.|-)(";

  std::map<std::string, PixelFormatRGB> stringToMatchingFormat;
  for (const auto &[channelOrder, channelOrderName] : ChannelOrderMapper)
  {
    for (auto alphaMode : {AlphaMode::None, AlphaMode::First, AlphaMode::Last})
    {
      for (auto [bitDepth, bitDepthString] : {std::pair<unsigned, std::string>{8, ""},
                                              {8, "8"},
                                              {10, "10"},
                                              {12, "12"},
                                              {16, "16"},
                                              {16, "64"},
                                              {16, "48"},
                                              {32, "32"},
                                              {32, "96"},
                                              {32, "128"}})
      {
        for (auto [endianness, endiannessName] :
             {std::pair<Endianness, std::string>{Endianness::Little, ""},
              {Endianness::Little, "le"},
              {Endianness::Big, "be"}})
        {
          std::string name;
          if (alphaMode == AlphaMode::First)
            name += "a";
          name += functions::toLower(channelOrderName);
          if (alphaMode == AlphaMode::Last)
            name += "a";
          name += bitDepthString + endiannessName;
          PixelFormatRGB formatMatchingString(
              bitDepth, DataLayout::Packed, channelOrder, alphaMode, endianness);

          stringToMatchingFormat[name] = formatMatchingString;
          matcher += name + "|";

          if (alphaMode == AlphaMode::First)
          {
            auto nameWithX   = name;
            auto formatWithX = formatMatchingString;
            nameWithX[0]     = 'x';
            formatWithX.setAlphaIgnored(true);
            stringToMatchingFormat[nameWithX] = formatWithX;
            matcher += nameWithX + "|";
          }
          else if (alphaMode == AlphaMode::Last)
          {
            auto nameWithX = name;
            const auto pos = nameWithX.find('a');
            if (pos != std::string::npos)
            {
              auto formatWithX = formatMatchingString;
              nameWithX[pos]   = 'x';
              formatWithX.setAlphaIgnored(true);
              stringToMatchingFormat[nameWithX] = formatWithX;
              matcher += nameWithX + "|";
            }
          }
        }
      }
    }
  }

  matcher.pop_back(); // Remove last |
  matcher += ")(?:_|\\.|-)";

  std::smatch sm;
  std::regex  strExpr(matcher);
  if (!std::regex_search(filename, sm, strExpr))
    return {};

  auto match     = sm.str(0);
  auto matchName = match.substr(1, match.size() - 2);

  auto format = stringToMatchingFormat[matchName];
  applySampleTypeFromTokens(format, filename);
  if (doesPixelFormatMatchFileSize(format, frameSize, fileSize))
  {
    const auto dataLayout = findDataLayoutInName(filename);
    format.setDataLayout(dataLayout);
    return format;
  }

  return {};
}

std::optional<PixelFormatRGB> checkForPixelFormatIndicatorInFileExtension(
    const std::string &filename, const Size &frameSize, const std::optional<std::int64_t> &fileSize)
{
  const auto fileExtension = std::filesystem::path(filename).extension();

  for (const auto &[channelOrder, name] : ChannelOrderMapper)
  {
    if (fileExtension == ("." + functions::toLower(name)))
    {
      auto format = PixelFormatRGB(8, DataLayout::Packed, channelOrder);
      applySampleTypeFromTokens(format, filename);
      if (doesPixelFormatMatchFileSize(format, frameSize, fileSize))
      {
        const auto dataLayout = findDataLayoutInName(filename);
        format.setDataLayout(dataLayout);
        return format;
      }
    }
  }
  return {};
}

std::optional<PixelFormatRGB> checkSpecificFileExtensions(
    const std::string &filename, const Size &frameSize, const std::optional<std::int64_t> &fileSize)
{
  const auto fileExtension = std::filesystem::path(filename).extension();

  if (fileExtension == ".cmyk")
  {
    auto format = PixelFormatRGB(8, DataLayout::Packed, ChannelOrder::RGB, AlphaMode::Last);
    applySampleTypeFromTokens(format, filename);
    if (doesPixelFormatMatchFileSize(format, frameSize, fileSize))
      return format;
  }

  return {};
}

PixelFormatRGB guessPixelFormatFromSizeAndName(const GuessedFrameFormat &guessedFrameFormat,
                                               const FileInfoForGuess   &fileInfo)
{
  if (!guessedFrameFormat.frameSize || fileInfo.filename.empty())
    return {};

  const auto filename  = functions::toLower(fileInfo.filename);
  const auto frameSize = *guessedFrameFormat.frameSize;
  const auto fileSize  = fileInfo.fileSize;

  // Helper lambda: If we detected a format (default 8 bit) but also parsed a bit depth from the
  // generic frame format guess (e.g. pattern '_10b_' or '10bit') and the format name did not
  // already contain an explicit bit depth indicator (we only generated names with an appended
  // number when we matched one explicitly), then apply that bit depth and verify file size.
  auto applyGuessedBitDepthIfReasonable = [&](PixelFormatRGB &fmt, const std::string &nameLower) {
    if (!guessedFrameFormat.bitDepth)
      return; // Nothing to apply

    // If bits already differ from 8 we must have matched an explicit pattern (e.g. rgb10) – keep it
    if (fmt.getBitsPerSample() != 8)
      return;

    // Check whether the name token we matched already included a bit depth substring. We treat
    // the presence of "rgb10", "rgb12", etc. (any channel order followed immediately by digits)
    // as explicit specification. If not present but we have a standalone _10b_ (handled earlier
    // by FrameFormatGuess) we upgrade here.
    bool hasExplicitBitDepthToken = false;
    for (const auto &[order, orderName] : ChannelOrderMapper)
    {
      const auto orderLower = functions::toLower(orderName);
      for (auto bd : {8, 10, 12, 16, 32})
      {
        const auto pattern = orderLower + std::to_string(bd); // e.g. rgb10
        if (nameLower.find(pattern) != std::string::npos)
        {
          hasExplicitBitDepthToken = true;
          break;
        }
      }
      if (hasExplicitBitDepthToken)
        break;
    }
    if (hasExplicitBitDepthToken)
      return;

    fmt.setBitsPerSample(*guessedFrameFormat.bitDepth);
    if (!doesPixelFormatMatchFileSize(fmt, frameSize, fileSize))
    {
      // Revert if file size no longer matches; leave at 8 bit default.
      fmt.setBitsPerSample(8);
    }
  };

  if (const auto pixelFormat = checkSpecificFileExtensions(filename, frameSize, fileSize))
  {
    auto fmt = *pixelFormat;
    applyGuessedBitDepthIfReasonable(fmt, filename);
    return fmt;
  }

  if (const auto pixelFormat = checkForPixelFormatIndicatorInName(filename, frameSize, fileSize))
  {
    auto fmt = *pixelFormat;
    applyGuessedBitDepthIfReasonable(fmt, filename);
    return fmt;
  }

  if (const auto pixelFormat =
          checkForPixelFormatIndicatorInFileExtension(filename, frameSize, fileSize))
  {
    auto fmt = *pixelFormat;
    applyGuessedBitDepthIfReasonable(fmt, filename);
    return fmt;
  }

  if (const auto pixelFormat = checkForPixelFormatIndicatorInName(
          functions::toLower(fileInfo.parentFolderName), frameSize, fileSize))
  {
    auto fmt = *pixelFormat;
    applyGuessedBitDepthIfReasonable(fmt, functions::toLower(fileInfo.parentFolderName));
    return fmt;
  }

  if (guessedFrameFormat.frameSize)
  {
    auto fmt = DEFAULT_PIXEL_FORMAT;
    applySampleTypeFromTokens(fmt, filename);
    applyGuessedBitDepthIfReasonable(fmt, filename);
    return fmt;
  }

  return {};
}

} // namespace video::rgb

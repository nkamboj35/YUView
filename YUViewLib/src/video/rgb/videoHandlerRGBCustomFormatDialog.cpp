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

#include "videoHandlerRGBCustomFormatDialog.h"

#include <common/Functions.h>

namespace video::rgb
{

videoHandlerRGBCustomFormatDialog::videoHandlerRGBCustomFormatDialog(
    const PixelFormatRGB &rgbFormat)
{
  this->ui.setupUi(this);

  this->ui.rgbOrderComboBox->addItems(functions::toQStringList(ChannelOrderMapper.getNames()));

  // Set the default (RGB no alpha)
  this->ui.rgbOrderComboBox->setCurrentIndex(0);
  this->ui.alphaChannelGroupBox->setChecked(false);
  this->ui.afterRGBRadioButton->setChecked(false);

  const auto hasAlphaChannel =
      rgbFormat.hasAlpha() || rgbFormat.isAlphaIgnored();
  if (hasAlphaChannel)
  {
    this->ui.alphaChannelGroupBox->setChecked(true);
    auto alphaPosition = rgbFormat.getChannelPosition(Channel::Alpha);
    this->ui.beforeRGBRadioButton->setChecked(alphaPosition == 0);
    this->ui.afterRGBRadioButton->setChecked(alphaPosition == 3);
  }
  this->ui.ignoreAlphaCheckBox->setChecked(rgbFormat.isAlphaIgnored());

  if (auto index = ChannelOrderMapper.indexOf(rgbFormat.getChannelOrder()))
  {
    this->ui.rgbOrderComboBox->setCurrentIndex(int(index));
  }

  this->ui.sampleTypeComboBox->addItems(
      functions::toQStringList(SampleTypeMapper.getNames()));
  this->ui.sampleTypeComboBox->setCurrentIndex(
      static_cast<int>(SampleTypeMapper.indexOf(rgbFormat.getSampleType())));

  auto bitDepth = rgbFormat.getBitsPerSample();
  this->ui.bitDepthSpinBox->setValue(bitDepth);
  this->ui.comboBoxEndianness->setEnabled(bitDepth > 8);
  this->ui.comboBoxEndianness->setCurrentIndex(rgbFormat.getEndianess() == Endianness::Big ? 0 : 1);

  const auto isFloatType = rgbFormat.getSampleType() != SampleType::UnsignedInteger &&
                           rgbFormat.getSampleType() != SampleType::SignedInteger;
  this->ui.bitDepthSpinBox->setEnabled(!isFloatType);

  this->ui.planarCheckBox->setChecked(rgbFormat.getDataLayout() == DataLayout::Planar);
}

PixelFormatRGB videoHandlerRGBCustomFormatDialog::getSelectedRGBFormat() const
{
  const auto channelOrderIndex = this->ui.rgbOrderComboBox->currentIndex();
  if (channelOrderIndex < 0)
    return {};
  auto channelOrder = ChannelOrderMapper.getValueAt(static_cast<std::size_t>(channelOrderIndex));
  if (!channelOrder)
    return {};

  auto bitDepth = this->ui.bitDepthSpinBox->value();

  auto dataLayout = DataLayout::Packed;
  if (this->ui.planarCheckBox->checkState() == Qt::Checked)
    dataLayout = DataLayout::Planar;

  auto alphaMode = AlphaMode::None;
  if (this->ui.alphaChannelGroupBox->isChecked())
  {
    if (this->ui.afterRGBRadioButton->isChecked())
      alphaMode = AlphaMode::Last;
    else
      alphaMode = AlphaMode::First;
  }

  auto endianness = Endianness::Little;
  if (this->ui.comboBoxEndianness->currentIndex() == 0)
    endianness = Endianness::Big;

  auto sampleType      = SampleType::UnsignedInteger;
  auto sampleTypeIndex = this->ui.sampleTypeComboBox->currentIndex();
  if (sampleTypeIndex >= 0)
    if (auto st = SampleTypeMapper.getValueAt(static_cast<std::size_t>(sampleTypeIndex)))
      sampleType = *st;

  bool alphaIgnored = this->ui.ignoreAlphaCheckBox->isChecked();

  return PixelFormatRGB(
      bitDepth, dataLayout, *channelOrder, alphaMode, endianness, sampleType, alphaIgnored);
}

void videoHandlerRGBCustomFormatDialog::on_bitDepthSpinBox_valueChanged(int value)
{
  this->ui.comboBoxEndianness->setEnabled(value > 8);
}

void videoHandlerRGBCustomFormatDialog::on_sampleTypeComboBox_currentIndexChanged(int index)
{
  if (index < 0)
    return;

  auto sampleType = SampleTypeMapper.getValueAt(static_cast<std::size_t>(index));
  if (!sampleType)
    return;

  switch (*sampleType)
  {
  case SampleType::Float16:
  case SampleType::BFloat16:
    this->ui.bitDepthSpinBox->setValue(16);
    this->ui.bitDepthSpinBox->setEnabled(false);
    break;
  case SampleType::Float32:
    this->ui.bitDepthSpinBox->setValue(32);
    this->ui.bitDepthSpinBox->setEnabled(false);
    break;
  default:
    this->ui.bitDepthSpinBox->setEnabled(true);
    break;
  }

  this->ui.comboBoxEndianness->setEnabled(this->ui.bitDepthSpinBox->value() > 8);
}

} // namespace video::rgb

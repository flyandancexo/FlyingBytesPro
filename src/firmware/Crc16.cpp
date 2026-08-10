// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware/Crc16.h"

quint16 crc16CcittFalse(QByteArrayView data) {
  quint16 crc = 0xFFFF;
  for (const char raw : data) {
    crc ^= static_cast<quint16>(static_cast<quint8>(raw)) << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x8000)
        ? static_cast<quint16>((crc << 1) ^ 0x1021)
        : static_cast<quint16>(crc << 1);
    }
  }
  return crc;
}

quint16 crc16CcittFalse(const FirmwareImage& image) {
  const QByteArray bytes = image.dataForRange(
    0, image.capacity(), image.erasedValue());
  return crc16CcittFalse(bytes);
}

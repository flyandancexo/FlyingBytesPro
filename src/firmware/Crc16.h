// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "firmware/FirmwareImage.h"

#include <QByteArrayView>
#include <QtGlobal>

quint16 crc16CcittFalse(QByteArrayView data);
quint16 crc16CcittFalse(const FirmwareImage& image);

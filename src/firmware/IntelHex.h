// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "firmware/FirmwareImage.h"

#include <QByteArray>
#include <QString>

class IntelHex
{
public:
    static bool parse(QByteArrayView text, FirmwareImage& image, QString& error);
    static QByteArray serialize(const FirmwareImage& image);

    static bool loadFile(const QString& path, FirmwareImage& image, QString& error);
    static bool saveFile(const QString& path, const FirmwareImage& image, QString& error);

private:
    static QByteArray makeRecord(quint8 type, quint16 address, QByteArrayView data);
};

// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "firmware/FirmwareImage.h"

#include <QString>
#include <QVector>

struct FlyingBytesProject {
  QString deviceId;
  int clockId = 0;
  FirmwareImage flash;
  FirmwareImage eeprom;
  QVector<int> preFuses;
  QVector<int> finalFuses;
  int lockValue = 0xFF;
  QVector<bool> taskSelections;
};

class ProjectFile {
public:
  static bool save(const QString& path, const FlyingBytesProject& project,
                   QString& error);
  static bool load(const QString& path, FlyingBytesProject& project,
                   QString& error);
};

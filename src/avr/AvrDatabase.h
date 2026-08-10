// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avr/AvrDevice.h"

#include <QByteArrayView>
#include <QVector>

class AvrDatabase {
public:
  bool load(QString& error);

  const QVector<AvrDevice>& devices() const;
  const AvrDevice* byId(const QString& id) const;
  const AvrDevice* bySignature(QByteArrayView signature) const;
  QVector<const AvrDevice*> bySignatureAll(QByteArrayView signature) const;
  QVector<const AvrDevice*> bySignatureDetectionCandidates(QByteArrayView signature) const;

private:
  QVector<AvrDevice> m_devices;
};

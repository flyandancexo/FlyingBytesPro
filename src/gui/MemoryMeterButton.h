// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "firmware/FirmwareImage.h"

#include <QAbstractButton>

class MemoryMeterButton : public QAbstractButton {
  Q_OBJECT

public:
  explicit MemoryMeterButton(const QString& title, QWidget* parent = nullptr);

  void setImage(const FirmwareImage& image);
  QString summaryText() const;
  quint16 crc() const;

  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

protected:
  void paintEvent(QPaintEvent* event) override;

private:
  QString m_title;
  qsizetype m_capacity = 0;
  qsizetype m_loaded = 0;
  quint16 m_crc = 0xFFFF;
};

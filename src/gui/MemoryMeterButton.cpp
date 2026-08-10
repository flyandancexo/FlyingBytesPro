// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/MemoryMeterButton.h"

#include "firmware/Crc16.h"
#include "gui/DisplayLanguage.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

MemoryMeterButton::MemoryMeterButton(const QString& title, QWidget* parent)
    : QAbstractButton(parent), m_title(title) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  setToolTip(DisplayLanguage::text(QStringLiteral("Read %1 from MCU"))
    .arg(DisplayLanguage::text(title)));
}

void MemoryMeterButton::setImage(const FirmwareImage& image) {
  m_capacity = image.capacity();
  m_loaded = image.definedCount();
  m_crc = crc16CcittFalse(image);
  setAccessibleName(summaryText());
  update();
}

QString MemoryMeterButton::summaryText() const {
  const double percent = m_capacity > 0
    ? 100.0 * static_cast<double>(m_loaded) / static_cast<double>(m_capacity)
    : 0.0;
  return DisplayLanguage::text(QStringLiteral("%1: %2/%3 Bytes %4%"))
    .arg(DisplayLanguage::text(m_title))
    .arg(m_loaded)
    .arg(m_capacity)
    .arg(percent, 0, 'f', 1);
}

quint16 MemoryMeterButton::crc() const {
  return m_crc;
}

QSize MemoryMeterButton::sizeHint() const {
  return QSize(340, 40);
}

QSize MemoryMeterButton::minimumSizeHint() const {
  return QSize(100, 40);
}

void MemoryMeterButton::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event)
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);

  const bool flash = m_title.compare(QStringLiteral("FLASH"), Qt::CaseInsensitive) == 0;
  const bool empty = m_loaded == 0;
  const qreal fraction = m_capacity > 0
    ? std::clamp(static_cast<qreal>(m_loaded) / static_cast<qreal>(m_capacity), 0.0, 1.0)
    : 0.0;
  const double percent = m_capacity > 0
    ? 100.0 * static_cast<double>(m_loaded) / static_cast<double>(m_capacity)
    : 0.0;

  const QColor emptyBackground(246, 247, 248);
  const QColor emptyBorder(231, 233, 236);
  const QColor background = flash ? QColor(231, 244, 255) : QColor(232, 249, 241);
  const QColor border = flash ? QColor(112, 174, 222) : QColor(105, 190, 151);
  const QColor text = empty ? QColor(232, 234, 237)
                            : (flash ? QColor(31, 75, 112) : QColor(25, 92, 65));

  const QRectF outer = rect().adjusted(1, 1, -1, -1);
  painter.setPen(QPen(hasFocus() ? QColor(38, 99, 169)
                                : (empty ? emptyBorder : border), 2));
  painter.setBrush(isDown() ? QColor(214, 218, 223)
                            : (empty ? emptyBackground : background));
  painter.drawRoundedRect(outer, 5, 5);

  QRectF fillRect = outer.adjusted(3, 3, -3, -3);
  fillRect.setWidth(fillRect.width() * fraction);
  if (!empty && fillRect.width() > 0.5) {
    QLinearGradient gradient(fillRect.topLeft(), fillRect.topRight());
    if (flash) {
      gradient.setColorAt(0.0, QColor(191, 226, 255));
      gradient.setColorAt(1.0, QColor(105, 184, 239));
    } else {
      gradient.setColorAt(0.0, QColor(187, 239, 215));
      gradient.setColorAt(1.0, QColor(104, 211, 166));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(fillRect, 4, 4);
  }

  QFont displayFont = font();
  displayFont.setBold(true);
  displayFont.setPointSizeF(std::max(7.0, displayFont.pointSizeF() - 0.2));
  painter.setFont(displayFont);
  painter.setPen(text);
  painter.drawText(QRectF(7, 1, width() - 14, height() - 2), Qt::AlignCenter,
    DisplayLanguage::text(QStringLiteral("%1: %2/%3 Bytes %4%"))
      .arg(DisplayLanguage::text(m_title))
      .arg(m_loaded)
      .arg(m_capacity)
      .arg(percent, 0, 'f', 1));
}

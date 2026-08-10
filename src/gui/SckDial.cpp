// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/SckDial.h"

#include "gui/DisplayLanguage.h"

#include "usb/UsbAspProtocol.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QtMath>

#include <algorithm>
#include <array>

namespace {

constexpr qreal kStartDegrees = 214.0;
constexpr qreal kEndDegrees = -34.0;
constexpr qreal kSweepDegrees = kStartDegrees - kEndDegrees;

QRectF faceRectFor(const QSize& widgetSize, const QSize& imageSize) {
  const QSize availableSize(
    std::max(1, widgetSize.width() - 4),
    std::max(1, widgetSize.height() - 2));
  const QSize sourceSize = imageSize.isValid() ? imageSize : availableSize;
  const QSize faceSize = sourceSize.scaled(availableSize, Qt::KeepAspectRatio);
  return QRectF(
    (widgetSize.width() - faceSize.width()) / 2.0,
    (widgetSize.height() - faceSize.height()) / 2.0,
    faceSize.width(), faceSize.height());
}

QPointF dialCenterFor(const QRectF& faceRect) {
  return QPointF(
    faceRect.left() + faceRect.width() * 0.50,
    faceRect.top() + faceRect.height() * 0.574);
}

qreal distanceFromSweep(qreal degrees) {
  if (degrees < kEndDegrees) {
    return kEndDegrees - degrees;
  }
  if (degrees > kStartDegrees) {
    return degrees - kStartDegrees;
  }
  return 0.0;
}

}

SckDial::SckDial(QWidget* parent)
    : QDial(parent) {
  m_clockIds = {
    static_cast<int>(usbasp::IspClock::Hz500),
    static_cast<int>(usbasp::IspClock::KHz1),
    static_cast<int>(usbasp::IspClock::KHz2),
    static_cast<int>(usbasp::IspClock::KHz4),
    static_cast<int>(usbasp::IspClock::KHz8),
    static_cast<int>(usbasp::IspClock::KHz16),
    static_cast<int>(usbasp::IspClock::KHz32),
    static_cast<int>(usbasp::IspClock::KHz93_75),
    static_cast<int>(usbasp::IspClock::KHz187_5),
    static_cast<int>(usbasp::IspClock::KHz375),
    static_cast<int>(usbasp::IspClock::KHz750),
    static_cast<int>(usbasp::IspClock::MHz1_5),
    static_cast<int>(usbasp::IspClock::MHz3),
    static_cast<int>(usbasp::IspClock::Auto),
    static_cast<int>(usbasp::IspClock::Pro)
  };
  m_labels = {
    QStringLiteral("500 Hz"), QStringLiteral("1 kHz"), QStringLiteral("2 kHz"),
    QStringLiteral("4 kHz"), QStringLiteral("8 kHz"), QStringLiteral("16 kHz"),
    QStringLiteral("32 kHz"), QStringLiteral("93.8 kHz"), QStringLiteral("187.5 kHz"),
    QStringLiteral("375 kHz"), QStringLiteral("750 kHz"), QStringLiteral("1.5 MHz"),
    QStringLiteral("3 MHz"), QStringLiteral("AUTO"), QStringLiteral("PRO")
  };

  setRange(0, static_cast<int>(m_clockIds.size()) - 1);
  setValue(maximum());
  setSingleStep(1);
  setPageStep(1);
  setWrapping(false);
  setNotchesVisible(false);
  setFocusPolicy(Qt::NoFocus);
  setToolTip(DisplayLanguage::text(QStringLiteral(
    "USBasp SPI SCK. Auto scans from the fastest supported clock downward; Pro uses the USBasp firmware default.")));

  connect(this, &QDial::valueChanged, this, [this](int) {
    setAccessibleName(DisplayLanguage::text(QStringLiteral("SPI clock %1")).arg(clockText()));
    update();
  });
}

int SckDial::clockId() const {
  const int maximumIndex = static_cast<int>(m_clockIds.size()) - 1;
  const int index = std::clamp(value(), 0, maximumIndex);
  return m_clockIds.at(index);
}

void SckDial::setClockId(int clockId) {
  const int index = m_clockIds.indexOf(clockId);
  const int proIndex = m_clockIds.indexOf(static_cast<int>(usbasp::IspClock::Pro));
  setValue(index >= 0 ? index : std::max(0, proIndex));
}

QString SckDial::clockText() const {
  const int maximumIndex = static_cast<int>(m_labels.size()) - 1;
  const int index = std::clamp(value(), 0, maximumIndex);
  return m_labels.at(index);
}

void SckDial::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event)
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing, true);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

  const QPixmap face(QStringLiteral(":/icons/clock_sck.png"));
  const QRectF faceRect = faceRectFor(size(), face.size());
  painter.drawPixmap(faceRect, face, face.rect());

  const int count = maximum() - minimum() + 1;
  const qreal fraction = count > 1
    ? static_cast<qreal>(value() - minimum()) / static_cast<qreal>(count - 1)
    : 0.0;

  // The supplied artwork uses a 248-degree scale from the lowest visible
  // left tick to the lowest visible right tick.
  const qreal degrees = kStartDegrees - fraction * kSweepDegrees;
  const qreal radians = qDegreesToRadians(degrees);
  const QPointF dialCenter = dialCenterFor(faceRect);
  const qreal innerRadius = faceRect.height() * 0.315;
  const qreal outerRadius = faceRect.height() * 0.395;
  const QPointF barInner(
    dialCenter.x() + qCos(radians) * innerRadius,
    dialCenter.y() - qSin(radians) * innerRadius);
  const QPointF barOuter(
    dialCenter.x() + qCos(radians) * outerRadius,
    dialCenter.y() - qSin(radians) * outerRadius);

  const auto selectedClock = static_cast<usbasp::IspClock>(clockId());
  const QColor markerColor = selectedClock == usbasp::IspClock::Auto
    ? QColor(24, 164, 98)
    : selectedClock == usbasp::IspClock::Pro
      ? QColor(218, 165, 32)
      : QColor(221, 54, 48);
  QPen markerPen(markerColor);
  markerPen.setWidthF(std::max<qreal>(2.8, faceRect.height() * 0.032));
  markerPen.setCapStyle(Qt::RoundCap);
  painter.setPen(markerPen);
  painter.setBrush(Qt::NoBrush);
  painter.drawLine(barInner, barOuter);

  const QString displayText = clockText();
  const qreal textWidth = faceRect.width() * 0.58;
  const qreal textHeight = faceRect.height() * 0.25;
  const QPointF textCenter(
    faceRect.center().x(),
    faceRect.top() + faceRect.height() * 0.465);
  const QRectF textRect(
    textCenter.x() - textWidth / 2.0,
    textCenter.y() - textHeight / 2.0,
    textWidth, textHeight);

  QFont centerFont = font();
  centerFont.setBold(true);
  centerFont.setPixelSize(std::max(11, qRound(faceRect.height() * 0.135)));
  QFontMetricsF metrics(centerFont);
  while (metrics.horizontalAdvance(displayText) > textRect.width()
         && centerFont.pixelSize() > 9) {
    centerFont.setPixelSize(centerFont.pixelSize() - 1);
    metrics = QFontMetricsF(centerFont);
  }

  painter.setFont(centerFont);
  const QColor textColor = selectedClock == usbasp::IspClock::Auto
    ? QColor(0, 126, 82)
    : selectedClock == usbasp::IspClock::Pro
      ? QColor(218, 165, 32)
      : QColor(40, 45, 50);
  painter.setPen(textColor);
  painter.setBrush(Qt::NoBrush);
  painter.drawText(textRect,
                   Qt::AlignCenter | Qt::TextSingleLine,
                   displayText);
}

void SckDial::setValueFromPosition(const QPointF& position) {
  const QPixmap face(QStringLiteral(":/icons/clock_sck.png"));
  const QRectF faceRect = faceRectFor(size(), face.size());
  const QPointF dialCenter = dialCenterFor(faceRect);

  qreal rawDegrees = qRadiansToDegrees(qAtan2(
    dialCenter.y() - position.y(),
    position.x() - dialCenter.x()));
  if (rawDegrees < 0.0) {
    rawDegrees += 360.0;
  }

  const std::array<qreal, 3> candidates{
    rawDegrees - 360.0,
    rawDegrees,
    rawDegrees + 360.0
  };
  qreal degrees = candidates.front();
  qreal bestDistance = distanceFromSweep(degrees);
  for (const qreal candidate : candidates) {
    const qreal candidateDistance = distanceFromSweep(candidate);
    if (candidateDistance < bestDistance) {
      degrees = candidate;
      bestDistance = candidateDistance;
    }
  }

  degrees = std::clamp(degrees, kEndDegrees, kStartDegrees);
  const qreal fraction = (kStartDegrees - degrees) / kSweepDegrees;
  const int newValue = minimum() + qRound(
    fraction * static_cast<qreal>(maximum() - minimum()));
  setValue(std::clamp(newValue, minimum(), maximum()));
}

void SckDial::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton) {
    QDial::mousePressEvent(event);
    return;
  }

  m_dragging = true;
  setValueFromPosition(event->position());
  event->accept();
}

void SckDial::mouseMoveEvent(QMouseEvent* event) {
  if (!m_dragging) {
    QDial::mouseMoveEvent(event);
    return;
  }

  setValueFromPosition(event->position());
  event->accept();
}

void SckDial::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !m_dragging) {
    QDial::mouseReleaseEvent(event);
    return;
  }

  setValueFromPosition(event->position());
  m_dragging = false;
  event->accept();
}

QSize SckDial::sizeHint() const {
  return QSize(132, 132);
}

QSize SckDial::minimumSizeHint() const {
  return QSize(112, 112);
}

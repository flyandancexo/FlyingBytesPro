// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDial>
#include <QStringList>
#include <QVector>

class QMouseEvent;
class QPaintEvent;
class QPointF;

class SckDial : public QDial {
  Q_OBJECT

public:
  explicit SckDial(QWidget* parent = nullptr);

  int clockId() const;
  void setClockId(int clockId);
  QString clockText() const;

protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  QSize sizeHint() const override;
  QSize minimumSizeHint() const override;

private:
  void setValueFromPosition(const QPointF& position);

  QVector<int> m_clockIds;
  QStringList m_labels;
  bool m_dragging = false;
};

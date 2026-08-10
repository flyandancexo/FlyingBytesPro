// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avr/AvrDevice.h"

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class FuseLockDialog : public QDialog {
  Q_OBJECT

public:
  explicit FuseLockDialog(const AvrDevice& device,
                          const QVector<int>& fuseValues,
                          int lockValue,
                          const QJsonObject& metadata,
                          bool prewrite,
                          QWidget* parent = nullptr);

  QVector<int> fuseValues() const;
  int lockValue() const;
  void setReadValues(const QVector<int>& values, int lockValue);
  void setStatus(const QString& text, bool success);
  void focusLockSection();

Q_SIGNALS:
  void readRequested();
  void writeFusesRequested(const QVector<int>& values);
  void writeLockRequested(int value);

protected:
  void closeEvent(QCloseEvent* event) override;

private:
  enum ByteIndex {
    LowByte = 0,
    HighByte = 1,
    ExtendedByte = 2,
    LockByte = 3
  };

  QLineEdit* createValueEdit(int byteIndex, bool supported, QWidget* parent);
  QWidget* createBitColumn(const QString& title, int byteIndex,
                           bool supported, quint8 readableMask,
                           quint8 writableMask, QWidget* parent);
  void buildDecodedOptions();
  void addOptionGroup(const QString& title, int byteIndex,
                      const QJsonArray& options);
  QJsonObject fuseMetadata(int byteIndex) const;
  QJsonObject bitMetadata(int byteIndex, int bit) const;
  QString bitName(int byteIndex, int bit) const;
  QString bitDescription(int byteIndex, int bit) const;
  bool parseRawEdits();
  void setByteValue(int byteIndex, int value);
  int byteValue(int byteIndex) const;
  void updateRawEdits();
  void updateBitControls();
  void updateDecodedChecks();
  void refreshUi();
  void applyOption(QTreeWidgetItem* item, bool checked);

  AvrDevice m_device;
  QJsonObject m_metadata;
  QVector<int> m_values{0xFF, 0xFF, 0xFF, 0xFF};
  QVector<QLineEdit*> m_valueEdits{nullptr, nullptr, nullptr, nullptr};
  QVector<QVector<QPushButton*>> m_bitButtons;
  QTreeWidget* m_optionsTree = nullptr;
  QTreeWidgetItem* m_lockRoot = nullptr;
  QLabel* m_statusLabel = nullptr;
  QPushButton* m_writeFusesButton = nullptr;
  QPushButton* m_writeLockButton = nullptr;
  bool m_updating = false;
};

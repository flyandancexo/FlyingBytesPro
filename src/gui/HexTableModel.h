// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "firmware/FirmwareImage.h"

#include <QAbstractTableModel>
#include <QList>
#include <QUndoStack>

class HexTableModel : public QAbstractTableModel {
  Q_OBJECT

public:
  explicit HexTableModel(bool flash, QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  int columnCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;
  bool setData(const QModelIndex& index, const QVariant& value,
               int role) override;

  void setImage(const FirmwareImage& image);
  const FirmwareImage& image() const;
  FirmwareImage imageCopy() const;

  void fill(quint8 value, bool markDefined);
  void clearBuffer();
  void clearOffsets(const QList<qsizetype>& offsets);
  void fillOffsets(const QList<qsizetype>& offsets, quint8 value, bool markDefined);
  void setClean();
  void setBootloaderHighlight(bool enabled, qsizetype startByte);
  bool isModified() const;
  QUndoStack* undoStack();

  void applyBytes(qsizetype offset, QByteArrayView bytes,
                  const QBitArray& defined);

Q_SIGNALS:
  void imageChanged();
  void modifiedChanged(bool modified);

private:
  qsizetype byteOffset(const QModelIndex& index) const;
  int addressDigits() const;
  void emitModifiedState();

  FirmwareImage m_image;
  bool m_flash = true;
  bool m_bootloaderHighlightEnabled = false;
  qsizetype m_bootloaderStartByte = 0;
  QUndoStack m_undo;
};

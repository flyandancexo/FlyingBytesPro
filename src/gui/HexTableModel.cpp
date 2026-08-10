// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/HexTableModel.h"

#include "gui/DisplayLanguage.h"

#include <QBitArray>
#include <QBrush>
#include <QColor>
#include <QFont>
#include <QUndoCommand>

#include <algorithm>
#include <utility>

namespace {

constexpr int kBytesPerRow = 16;
constexpr int kAddressColumn = 0;
constexpr int kFirstByteColumn = 1;
constexpr int kLastByteColumn = 16;
constexpr int kDecimalAddressColumn = 17;
constexpr int kAsciiColumn = 18;

class ReplaceBytesCommand final : public QUndoCommand {
public:
  ReplaceBytesCommand(HexTableModel* model, qsizetype offset,
                      QByteArray oldBytes, QBitArray oldDefined,
                      QByteArray newBytes, QBitArray newDefined,
                      QString text = {})
      : m_model(model),
        m_offset(offset),
        m_oldBytes(std::move(oldBytes)),
        m_oldDefined(std::move(oldDefined)),
        m_newBytes(std::move(newBytes)),
        m_newDefined(std::move(newDefined)) {
    setText(text.isEmpty()
      ? QStringLiteral("Edit bytes at 0x%1")
          .arg(offset, 8, 16, QLatin1Char('0'))
      : std::move(text));
  }

  void undo() override {
    m_model->applyBytes(m_offset, m_oldBytes, m_oldDefined);
  }

  void redo() override {
    m_model->applyBytes(m_offset, m_newBytes, m_newDefined);
  }

private:
  HexTableModel* m_model = nullptr;
  qsizetype m_offset = 0;
  QByteArray m_oldBytes;
  QBitArray m_oldDefined;
  QByteArray m_newBytes;
  QBitArray m_newDefined;
};

QChar printableAscii(quint8 value) {
  if (value >= 0x20 && value <= 0x7E) {
    return QChar::fromLatin1(static_cast<char>(value));
  }
  return QChar(u'\u00B7');
}

} // namespace

HexTableModel::HexTableModel(bool flash, QObject* parent)
    : QAbstractTableModel(parent),
      m_flash(flash) {
  connect(&m_undo, &QUndoStack::cleanChanged, this, [this](bool) {
    emitModifiedState();
  });
}

int HexTableModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>((m_image.capacity() + kBytesPerRow - 1) / kBytesPerRow);
}

int HexTableModel::columnCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : 19;
}

QVariant HexTableModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};

  const qsizetype rowBase = static_cast<qsizetype>(index.row()) * kBytesPerRow;

  if (role == Qt::BackgroundRole && m_flash) {
    const bool addressCell = index.column() == kAddressColumn
      || index.column() == kDecimalAddressColumn;
    const bool bootRow = m_bootloaderHighlightEnabled
      && rowBase >= m_bootloaderStartByte;
    if (addressCell && bootRow) {
      const bool alternate = (index.row() & 1) != 0;
      return QBrush(alternate ? QColor(218, 237, 252)
                              : QColor(234, 246, 255));
    }
  }

  if (index.column() == kAddressColumn) {
    if (role == Qt::DisplayRole) {
      return QStringLiteral("0x%1")
        .arg(rowBase, addressDigits(), 16, QLatin1Char('0'))
        .toUpper();
    }
    if (role == Qt::TextAlignmentRole) {
      return static_cast<int>(Qt::AlignCenter);
    }
    if (role == Qt::FontRole) return QFont(QStringLiteral("Consolas"));
    return {};
  }

  if (index.column() == kDecimalAddressColumn) {
    if (role == Qt::DisplayRole) {
      return QString::number(rowBase);
    }
    if (role == Qt::TextAlignmentRole) {
      return static_cast<int>(Qt::AlignCenter);
    }
    if (role == Qt::FontRole) return QFont(QStringLiteral("Consolas"));
    return {};
  }

  if (index.column() >= kFirstByteColumn && index.column() <= kLastByteColumn) {
    const qsizetype offset = byteOffset(index);
    if (offset < 0 || offset >= m_image.capacity()) return {};

    if (role == Qt::DisplayRole || role == Qt::EditRole) {
      return QStringLiteral("%1")
        .arg(m_image.byteAt(offset), 2, 16, QLatin1Char('0'))
        .toUpper();
    }
    if (role == Qt::TextAlignmentRole) {
      return static_cast<int>(Qt::AlignCenter);
    }
    if (role == Qt::FontRole) return QFont(QStringLiteral("Consolas"));
    if (role == Qt::ForegroundRole) {
      return QBrush(m_image.isDefined(offset)
        ? (m_flash ? QColor(20, 70, 190) : QColor(45, 155, 87))
        : QColor(128, 128, 128));
    }
    if (role == Qt::ToolTipRole) {
      return DisplayLanguage::text(QStringLiteral("Address 0x%1 (%2)"))
        .arg(offset, addressDigits(), 16, QLatin1Char('0'))
        .arg(DisplayLanguage::text(m_image.isDefined(offset)
          ? QStringLiteral("defined")
          : QStringLiteral("undefined")));
    }
    return {};
  }

  if (index.column() == kAsciiColumn) {
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
      QString ascii;
      ascii.reserve(kBytesPerRow);
      for (int i = 0; i < kBytesPerRow; ++i) {
        const qsizetype offset = rowBase + i;
        if (offset >= m_image.capacity()) {
          ascii += QLatin1Char(' ');
        } else if (!m_image.isDefined(offset)) {
          ascii += QChar(u'\u00B7');
        } else {
          ascii += printableAscii(m_image.byteAt(offset));
        }
      }
      return ascii;
    }
    if (role == Qt::TextAlignmentRole) {
      return static_cast<int>(Qt::AlignCenter);
    }
    if (role == Qt::FontRole) return QFont(QStringLiteral("Consolas"));
    if (role == Qt::ToolTipRole) {
      return DisplayLanguage::text(QStringLiteral(
        "Edit printable ASCII directly. Middle dots preserve the current byte."));
    }
  }
  return {};
}

QVariant HexTableModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
  if (orientation != Qt::Horizontal) return {};
  if (role == Qt::TextAlignmentRole) {
    if (section == kAsciiColumn) {
      return static_cast<int>(Qt::AlignCenter);
    }
    return static_cast<int>(Qt::AlignCenter);
  }
  if (role == Qt::FontRole) {
    QFont font(QStringLiteral("Consolas"));
    font.setBold(true);
    return font;
  }
  if (role != Qt::DisplayRole) return {};
  if (section == kAddressColumn) return DisplayLanguage::text(QStringLiteral("HEX Address"));
  if (section >= kFirstByteColumn && section <= kLastByteColumn) {
    return QStringLiteral("%1")
      .arg(section - kFirstByteColumn, 2, 16, QLatin1Char('0'))
      .toUpper();
  }
  if (section == kDecimalAddressColumn) return DisplayLanguage::text(QStringLiteral("DEC Address"));
  if (section == kAsciiColumn) return QStringLiteral("0123456789ABCDEF");
  return {};
}

Qt::ItemFlags HexTableModel::flags(const QModelIndex& index) const {
  Qt::ItemFlags result = QAbstractTableModel::flags(index);
  if (!index.isValid()) return result;

  if (index.column() >= kFirstByteColumn && index.column() <= kLastByteColumn
      && byteOffset(index) < m_image.capacity()) {
    result |= Qt::ItemIsEditable;
  } else if (index.column() == kAsciiColumn
             && static_cast<qsizetype>(index.row()) * kBytesPerRow < m_image.capacity()) {
    result |= Qt::ItemIsEditable;
  }
  return result;
}

bool HexTableModel::setData(const QModelIndex& index, const QVariant& value, int role) {
  if (role != Qt::EditRole || !index.isValid()) return false;

  if (index.column() >= kFirstByteColumn && index.column() <= kLastByteColumn) {
    const qsizetype offset = byteOffset(index);
    if (offset < 0 || offset >= m_image.capacity()) return false;

    QString text = value.toString().trimmed();
    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) text.remove(0, 2);
    if (text.size() != 2) return false;
    bool ok = false;
    const int byte = text.toInt(&ok, 16);
    if (!ok || byte < 0 || byte > 0xFF) return false;

    QByteArray oldBytes(1, static_cast<char>(m_image.byteAt(offset)));
    QBitArray oldDefined(1, m_image.isDefined(offset));
    QByteArray newBytes(1, static_cast<char>(byte));
    QBitArray newDefined(1, true);
    m_undo.push(new ReplaceBytesCommand(
      this, offset, oldBytes, oldDefined, newBytes, newDefined));
    return true;
  }

  if (index.column() == kAsciiColumn) {
    const qsizetype offset = static_cast<qsizetype>(index.row()) * kBytesPerRow;
    if (offset < 0 || offset >= m_image.capacity()) return false;

    const qsizetype length = std::min<qsizetype>(kBytesPerRow, m_image.capacity() - offset);
    QString text = value.toString();
    if (text.size() > length) text.truncate(length);

    QByteArray oldBytes(length, Qt::Uninitialized);
    QByteArray newBytes(length, Qt::Uninitialized);
    QBitArray oldDefined(length);
    QBitArray newDefined(length);
    bool changed = false;
    for (qsizetype i = 0; i < length; ++i) {
      const quint8 current = m_image.byteAt(offset + i);
      oldBytes[i] = static_cast<char>(current);
      newBytes[i] = static_cast<char>(current);
      oldDefined.setBit(i, m_image.isDefined(offset + i));
      newDefined.setBit(i, m_image.isDefined(offset + i));

      if (i >= text.size()) continue;
      const QChar character = text.at(i);
      if (character == QChar(u'\u00B7')) continue;
      const char replacement = character.toLatin1();
      if (replacement == '\0' && character.unicode() != 0) continue;
      if (static_cast<quint8>(replacement) != current || !m_image.isDefined(offset + i)) {
        newBytes[i] = replacement;
        newDefined.setBit(i, true);
        changed = true;
      }
    }
    if (!changed) return false;
    m_undo.push(new ReplaceBytesCommand(
      this, offset, oldBytes, oldDefined, newBytes, newDefined));
    return true;
  }

  return false;
}

void HexTableModel::setImage(const FirmwareImage& image) {
  beginResetModel();
  m_image = image;
  m_undo.clear();
  m_undo.setClean();
  endResetModel();
  Q_EMIT imageChanged();
  emitModifiedState();
}

const FirmwareImage& HexTableModel::image() const {
  return m_image;
}

FirmwareImage HexTableModel::imageCopy() const {
  return m_image;
}

void HexTableModel::fill(quint8 value, bool markDefined) {
  const qsizetype count = m_image.capacity();
  if (count <= 0) return;

  const QByteArray oldBytes = m_image.bytes();
  const QBitArray oldDefined = m_image.definedMask();
  QByteArray newBytes(count, static_cast<char>(value));
  QBitArray newDefined(count, markDefined);
  if (oldBytes == newBytes && oldDefined == newDefined) return;

  m_undo.push(new ReplaceBytesCommand(
    this, 0, oldBytes, oldDefined, std::move(newBytes), std::move(newDefined),
    markDefined && value == 0xFF
      ? DisplayLanguage::text(QStringLiteral("Fill buffer with FF"))
      : DisplayLanguage::text(QStringLiteral("Fill buffer"))));
}

void HexTableModel::clearBuffer() {
  const qsizetype count = m_image.capacity();
  if (count <= 0) return;

  const QByteArray oldBytes = m_image.bytes();
  const QBitArray oldDefined = m_image.definedMask();
  QByteArray newBytes(count, static_cast<char>(m_image.erasedValue()));
  QBitArray newDefined(count, false);
  if (oldBytes == newBytes && oldDefined == newDefined) return;

  m_undo.push(new ReplaceBytesCommand(
    this, 0, oldBytes, oldDefined, std::move(newBytes), std::move(newDefined),
    DisplayLanguage::text(QStringLiteral("Clear buffer"))));
}

void HexTableModel::clearOffsets(const QList<qsizetype>& offsets) {
  if (offsets.isEmpty() || m_image.capacity() <= 0) return;

  QList<qsizetype> sorted;
  sorted.reserve(offsets.size());
  for (qsizetype offset : offsets) {
    if (offset >= 0 && offset < m_image.capacity()) sorted.append(offset);
  }
  if (sorted.isEmpty()) return;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  struct Range {
    qsizetype start = 0;
    qsizetype length = 0;
  };
  QList<Range> ranges;
  qsizetype start = sorted.first();
  qsizetype previous = start;
  for (qsizetype i = 1; i < sorted.size(); ++i) {
    if (sorted.at(i) == previous + 1) {
      previous = sorted.at(i);
      continue;
    }
    ranges.append({start, previous - start + 1});
    start = sorted.at(i);
    previous = start;
  }
  ranges.append({start, previous - start + 1});

  QList<Range> changedRanges;
  for (const Range& range : ranges) {
    bool changed = false;
    for (qsizetype i = 0; i < range.length; ++i) {
      const qsizetype offset = range.start + i;
      if (m_image.isDefined(offset)
          || m_image.byteAt(offset) != m_image.erasedValue()) {
        changed = true;
        break;
      }
    }
    if (changed) changedRanges.append(range);
  }
  if (changedRanges.isEmpty()) return;

  m_undo.beginMacro(DisplayLanguage::text(QStringLiteral("Clear selected bytes")));
  for (const Range& range : changedRanges) {
    QByteArray oldBytes(range.length, Qt::Uninitialized);
    QBitArray oldDefined(range.length);
    for (qsizetype i = 0; i < range.length; ++i) {
      oldBytes[i] = static_cast<char>(m_image.byteAt(range.start + i));
      oldDefined.setBit(i, m_image.isDefined(range.start + i));
    }
    QByteArray newBytes(range.length, static_cast<char>(m_image.erasedValue()));
    QBitArray newDefined(range.length, false);
    m_undo.push(new ReplaceBytesCommand(
      this, range.start, std::move(oldBytes), std::move(oldDefined),
      std::move(newBytes), std::move(newDefined),
      DisplayLanguage::text(QStringLiteral("Clear selected bytes"))));
  }
  m_undo.endMacro();
}

void HexTableModel::fillOffsets(const QList<qsizetype>& offsets,
                                quint8 value, bool markDefined) {
  if (offsets.isEmpty() || m_image.capacity() <= 0) return;

  QList<qsizetype> sorted;
  sorted.reserve(offsets.size());
  for (qsizetype offset : offsets) {
    if (offset >= 0 && offset < m_image.capacity()) sorted.append(offset);
  }
  if (sorted.isEmpty()) return;
  std::sort(sorted.begin(), sorted.end());
  sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

  struct Range {
    qsizetype start = 0;
    qsizetype length = 0;
  };
  QList<Range> ranges;
  qsizetype start = sorted.first();
  qsizetype previous = start;
  for (qsizetype i = 1; i < sorted.size(); ++i) {
    if (sorted.at(i) == previous + 1) {
      previous = sorted.at(i);
      continue;
    }
    ranges.append({start, previous - start + 1});
    start = sorted.at(i);
    previous = start;
  }
  ranges.append({start, previous - start + 1});

  QList<Range> changedRanges;
  for (const Range& range : ranges) {
    bool changed = false;
    for (qsizetype i = 0; i < range.length; ++i) {
      const qsizetype offset = range.start + i;
      if (m_image.byteAt(offset) != value
          || m_image.isDefined(offset) != markDefined) {
        changed = true;
        break;
      }
    }
    if (changed) changedRanges.append(range);
  }
  if (changedRanges.isEmpty()) return;

  const QString action = markDefined && value == 0xFF
    ? DisplayLanguage::text(QStringLiteral("Zero selected bytes"))
    : DisplayLanguage::text(QStringLiteral("Fill selected bytes"));
  m_undo.beginMacro(action);
  for (const Range& range : changedRanges) {
    QByteArray oldBytes(range.length, Qt::Uninitialized);
    QBitArray oldDefined(range.length);
    for (qsizetype i = 0; i < range.length; ++i) {
      oldBytes[i] = static_cast<char>(m_image.byteAt(range.start + i));
      oldDefined.setBit(i, m_image.isDefined(range.start + i));
    }
    QByteArray newBytes(range.length, static_cast<char>(value));
    QBitArray newDefined(range.length, markDefined);
    m_undo.push(new ReplaceBytesCommand(
      this, range.start, std::move(oldBytes), std::move(oldDefined),
      std::move(newBytes), std::move(newDefined), action));
  }
  m_undo.endMacro();
}

void HexTableModel::setClean() {
  m_undo.setClean();
  emitModifiedState();
}

void HexTableModel::setBootloaderHighlight(bool enabled, qsizetype startByte) {
  const qsizetype boundedStart = std::clamp<qsizetype>(
    startByte, 0, m_image.capacity());
  if (m_bootloaderHighlightEnabled == enabled
      && m_bootloaderStartByte == boundedStart) {
    return;
  }
  m_bootloaderHighlightEnabled = enabled;
  m_bootloaderStartByte = boundedStart;
  if (rowCount() > 0 && columnCount() > 0) {
    Q_EMIT dataChanged(index(0, 0),
                       index(rowCount() - 1, columnCount() - 1),
                       {Qt::BackgroundRole});
  }
}

bool HexTableModel::isModified() const {
  return !m_undo.isClean();
}

QUndoStack* HexTableModel::undoStack() {
  return &m_undo;
}

void HexTableModel::applyBytes(qsizetype offset, QByteArrayView bytes,
                               const QBitArray& defined) {
  if (offset < 0 || offset >= m_image.capacity() || bytes.isEmpty()) return;
  const qsizetype count = std::min(bytes.size(), m_image.capacity() - offset);
  for (qsizetype i = 0; i < count; ++i) {
    m_image.setByte(offset + i, static_cast<quint8>(bytes.at(i)),
                    i < defined.size() && defined.testBit(i));
  }
  const int firstRow = static_cast<int>(offset / kBytesPerRow);
  const int lastRow = static_cast<int>((offset + count - 1) / kBytesPerRow);
  Q_EMIT dataChanged(index(firstRow, kFirstByteColumn), index(lastRow, kAsciiColumn),
                     {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole,
                      Qt::ToolTipRole});
  Q_EMIT imageChanged();
}

qsizetype HexTableModel::byteOffset(const QModelIndex& index) const {
  return static_cast<qsizetype>(index.row()) * kBytesPerRow
    + static_cast<qsizetype>(index.column() - kFirstByteColumn);
}

int HexTableModel::addressDigits() const {
  quint64 maxAddress = m_image.capacity() > 0
    ? static_cast<quint64>(m_image.capacity() - 1) : 0;
  int digits = 1;
  while (maxAddress >= 16) {
    maxAddress >>= 4u;
    ++digits;
  }
  return std::max(4, digits);
}

void HexTableModel::emitModifiedState() {
  Q_EMIT modifiedChanged(isModified());
}

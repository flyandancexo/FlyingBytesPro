// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrDatabase.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <utility>

namespace {

int naturalNameCompare(const QString& left, const QString& right) {
  qsizetype leftPos = 0;
  qsizetype rightPos = 0;

  while (leftPos < left.size() && rightPos < right.size()) {
    const bool leftDigit = left.at(leftPos).isDigit();
    const bool rightDigit = right.at(rightPos).isDigit();

    if (leftDigit && rightDigit) {
      qsizetype leftEnd = leftPos;
      qsizetype rightEnd = rightPos;
      while (leftEnd < left.size() && left.at(leftEnd).isDigit()) ++leftEnd;
      while (rightEnd < right.size() && right.at(rightEnd).isDigit()) ++rightEnd;

      qsizetype leftNumber = leftPos;
      qsizetype rightNumber = rightPos;
      while (leftNumber + 1 < leftEnd && left.at(leftNumber) == QLatin1Char('0')) ++leftNumber;
      while (rightNumber + 1 < rightEnd && right.at(rightNumber) == QLatin1Char('0')) ++rightNumber;

      const qsizetype leftDigits = leftEnd - leftNumber;
      const qsizetype rightDigits = rightEnd - rightNumber;
      if (leftDigits != rightDigits) return leftDigits < rightDigits ? -1 : 1;

      const int numberCompare = left.mid(leftNumber, leftDigits).compare(
        right.mid(rightNumber, rightDigits), Qt::CaseInsensitive);
      if (numberCompare != 0) return numberCompare;

      leftPos = leftEnd;
      rightPos = rightEnd;
      continue;
    }

    if (leftDigit != rightDigit) {
      return leftDigit ? -1 : 1;
    }

    qsizetype leftEnd = leftPos;
    qsizetype rightEnd = rightPos;
    while (leftEnd < left.size() && !left.at(leftEnd).isDigit()) ++leftEnd;
    while (rightEnd < right.size() && !right.at(rightEnd).isDigit()) ++rightEnd;

    const int textCompare = left.mid(leftPos, leftEnd - leftPos).compare(
      right.mid(rightPos, rightEnd - rightPos), Qt::CaseInsensitive);
    if (textCompare != 0) return textCompare;

    leftPos = leftEnd;
    rightPos = rightEnd;
  }

  if (leftPos == left.size() && rightPos == right.size()) return 0;
  return leftPos == left.size() ? -1 : 1;
}


struct AtmegaSortParts {
  bool valid = false;
  quint64 group = 0;
  QString modelDigits;
  QString suffix;
};

AtmegaSortParts atmegaSortParts(const QString& name) {
  static const QString prefix = QStringLiteral("ATmega");
  if (!name.startsWith(prefix, Qt::CaseInsensitive)) return {};

  qsizetype pos = prefix.size();
  if (pos >= name.size() || !name.at(pos).isDigit()) return {};
  qsizetype end = pos;
  while (end < name.size() && name.at(end).isDigit()) ++end;

  bool ok = false;
  const QString digits = name.mid(pos, end - pos);
  const quint64 model = digits.toULongLong(&ok, 10);
  if (!ok) return {};

  static constexpr quint64 groups[] = {256, 128, 88, 64, 48, 32, 16, 8};
  quint64 group = model;
  for (const quint64 candidate : groups) {
    const QString candidateText = QString::number(candidate);
    if (digits.startsWith(candidateText)) {
      group = candidate;
      break;
    }
  }

  return {true, group, digits, name.mid(end)};
}

bool naturalDeviceLess(const AvrDevice& left, const AvrDevice& right) {
  const AtmegaSortParts leftMega = atmegaSortParts(left.name);
  const AtmegaSortParts rightMega = atmegaSortParts(right.name);
  if (leftMega.valid && rightMega.valid) {
    if (leftMega.group != rightMega.group) return leftMega.group < rightMega.group;
    const int modelCompare = leftMega.modelDigits.compare(
      rightMega.modelDigits, Qt::CaseInsensitive);
    if (modelCompare != 0) return modelCompare < 0;
    const int suffixCompare = naturalNameCompare(leftMega.suffix, rightMega.suffix);
    if (suffixCompare != 0) return suffixCompare < 0;
  }

  const int nameCompare = naturalNameCompare(left.name, right.name);
  if (nameCompare != 0) return nameCompare < 0;
  return left.id.compare(right.id, Qt::CaseInsensitive) < 0;
}

} // namespace

bool AvrDatabase::load(QString& error) {
  QFile file(QStringLiteral(":/devices/avr_devices.json"));
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Cannot open the embedded AVR device database.");
    return false;
  }

  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QStringLiteral("AVR database JSON error: %1").arg(parseError.errorString());
    return false;
  }

  const QJsonObject root = document.object();
  if (root.value(QStringLiteral("schemaVersion")).toInt(-1) != 1) {
    error = QStringLiteral("Unsupported AVR database schema version.");
    return false;
  }

  QVector<AvrDevice> loaded;
  const auto array = root.value(QStringLiteral("devices")).toArray();
  loaded.reserve(array.size());
  for (const auto& value : array) {
    QString entryError;
    AvrDevice device = AvrDevice::fromJson(value.toObject(), entryError);
    if (!device.isValid()) {
      error = entryError;
      return false;
    }
    loaded.append(std::move(device));
  }

  if (loaded.isEmpty()) {
    error = QStringLiteral("The AVR device database is empty.");
    return false;
  }

  std::stable_sort(loaded.begin(), loaded.end(), naturalDeviceLess);
  m_devices = std::move(loaded);
  error.clear();
  return true;
}

const QVector<AvrDevice>& AvrDatabase::devices() const {
  return m_devices;
}

const AvrDevice* AvrDatabase::byId(const QString& id) const {
  for (const auto& device : m_devices) {
    if (device.id.compare(id, Qt::CaseInsensitive) == 0) {
      return &device;
    }
  }
  return nullptr;
}

const AvrDevice* AvrDatabase::bySignature(QByteArrayView signature) const {
  for (const auto& device : m_devices) {
    if (QByteArrayView(device.signature) == signature) {
      return &device;
    }
  }
  return nullptr;
}

QVector<const AvrDevice*> AvrDatabase::bySignatureAll(QByteArrayView signature) const {
  QVector<const AvrDevice*> matches;
  for (const auto& device : m_devices) {
    if (QByteArrayView(device.signature) == signature) {
      matches.append(&device);
    }
  }
  return matches;
}

QVector<const AvrDevice*> AvrDatabase::bySignatureDetectionCandidates(
    QByteArrayView signature) const {
  const QVector<const AvrDevice*> matches = bySignatureAll(signature);
  if (matches.size() < 2) return matches;

  QVector<const AvrDevice*> candidates;
  candidates.reserve(matches.size());
  for (const AvrDevice* candidate : matches) {
    bool trailingAAlias = false;
    if (candidate->name.endsWith(QLatin1Char('A'), Qt::CaseInsensitive)) {
      const QString baseName = candidate->name.left(candidate->name.size() - 1);
      for (const AvrDevice* other : matches) {
        if (other != candidate
            && other->name.compare(baseName, Qt::CaseInsensitive) == 0) {
          trailingAAlias = true;
          break;
        }
      }
    }
    if (!trailingAAlias) candidates.append(candidate);
  }

  return candidates.isEmpty() ? matches : candidates;
}

// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware/ProjectFile.h"
#include "usb/UsbAspProtocol.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>

#include <utility>

namespace {

QByteArray packDefinedMask(const FirmwareImage& image) {
  QByteArray packed((image.capacity() + 7) / 8, '\0');
  for (qsizetype index = 0; index < image.capacity(); ++index) {
    if (image.isDefined(index)) {
      packed[index / 8] = static_cast<char>(
        static_cast<quint8>(packed.at(index / 8)) | (1 << (index % 8)));
    }
  }
  return packed;
}

QJsonObject imageToJson(const FirmwareImage& image) {
  QJsonObject object;
  object.insert(QStringLiteral("capacity"), static_cast<qint64>(image.capacity()));
  object.insert(QStringLiteral("erasedValue"), image.erasedValue());
  object.insert(QStringLiteral("bytes"), QString::fromLatin1(
    qCompress(image.bytes(), 9).toBase64(QByteArray::Base64Encoding)));
  object.insert(QStringLiteral("definedMask"), QString::fromLatin1(
    qCompress(packDefinedMask(image), 9).toBase64(QByteArray::Base64Encoding)));
  return object;
}

bool imageFromJson(const QJsonObject& object, FirmwareImage& image, QString& error) {
  const qint64 capacity64 = object.value(QStringLiteral("capacity")).toInteger(-1);
  const int erasedValue = object.value(QStringLiteral("erasedValue")).toInt(0xFF);
  if (capacity64 < 0 || capacity64 > 16 * 1024 * 1024
      || erasedValue < 0 || erasedValue > 0xFF) {
    error = QStringLiteral("Project contains invalid memory geometry.");
    return false;
  }

  const QByteArray bytes = qUncompress(QByteArray::fromBase64(
    object.value(QStringLiteral("bytes")).toString().toLatin1()));
  const QByteArray mask = qUncompress(QByteArray::fromBase64(
    object.value(QStringLiteral("definedMask")).toString().toLatin1()));
  const qsizetype capacity = static_cast<qsizetype>(capacity64);
  if (bytes.size() != capacity || mask.size() != (capacity + 7) / 8) {
    error = QStringLiteral("Project memory data is incomplete or corrupt.");
    return false;
  }

  image.reset(capacity, static_cast<quint8>(erasedValue));
  for (qsizetype index = 0; index < capacity; ++index) {
    const bool defined = (static_cast<quint8>(mask.at(index / 8))
                          & (1 << (index % 8))) != 0;
    image.setByte(index, static_cast<quint8>(bytes.at(index)), defined);
  }
  return true;
}

QJsonArray intsToJson(const QVector<int>& values) {
  QJsonArray array;
  for (const int value : values) {
    array.append(value);
  }
  return array;
}

QVector<int> intsFromJson(const QJsonValue& value) {
  QVector<int> values;
  for (const QJsonValue item : value.toArray()) {
    values.append(item.toInt(-1));
  }
  return values;
}

} // namespace

bool ProjectFile::save(const QString& path, const FlyingBytesProject& project,
                       QString& error) {
  QJsonObject root;
  root.insert(QStringLiteral("format"), QStringLiteral("FlyingBytesPro Project"));
  root.insert(QStringLiteral("schemaVersion"), 2);
  root.insert(QStringLiteral("createdBy"), QStringLiteral("FlyingBytesPro V3.2.28"));
  root.insert(QStringLiteral("deviceId"), project.deviceId);
  root.insert(QStringLiteral("clockId"), project.clockId);
  root.insert(QStringLiteral("flash"), imageToJson(project.flash));
  root.insert(QStringLiteral("eeprom"), imageToJson(project.eeprom));
  root.insert(QStringLiteral("preFuses"), intsToJson(project.preFuses));
  root.insert(QStringLiteral("finalFuses"), intsToJson(project.finalFuses));
  root.insert(QStringLiteral("lockValue"), project.lockValue);

  QJsonArray tasks;
  for (const bool selected : project.taskSelections) {
    tasks.append(selected);
  }
  root.insert(QStringLiteral("tasks"), tasks);

  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    error = QStringLiteral("Cannot open %1 for writing: %2")
      .arg(path, file.errorString());
    return false;
  }
  const QByteArray encoded = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (file.write(encoded) != encoded.size() || !file.commit()) {
    error = QStringLiteral("Cannot save %1 completely: %2")
      .arg(path, file.errorString());
    return false;
  }
  error.clear();
  return true;
}

bool ProjectFile::load(const QString& path, FlyingBytesProject& project,
                       QString& error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    error = QStringLiteral("Cannot open %1 for reading: %2")
      .arg(path, file.errorString());
    return false;
  }

  QJsonParseError parseError{};
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QStringLiteral("Project JSON error: %1").arg(parseError.errorString());
    return false;
  }
  const QJsonObject root = document.object();
  const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(-1);
  if (root.value(QStringLiteral("format")).toString()
        != QStringLiteral("FlyingBytesPro Project")
      || (schemaVersion != 1 && schemaVersion != 2)) {
    error = QStringLiteral("This is not a supported FlyingBytesPro project file.");
    return false;
  }

  FlyingBytesProject loaded;
  loaded.deviceId = root.value(QStringLiteral("deviceId")).toString();
  loaded.clockId = root.value(QStringLiteral("clockId")).toInt(0);
  if (schemaVersion == 1 && loaded.clockId == 0) {
    loaded.clockId = static_cast<int>(usbasp::IspClock::Auto);
  }
  loaded.preFuses = intsFromJson(root.value(QStringLiteral("preFuses")));
  loaded.finalFuses = intsFromJson(root.value(QStringLiteral("finalFuses")));
  loaded.lockValue = root.value(QStringLiteral("lockValue")).toInt(0xFF);
  for (const QJsonValue item : root.value(QStringLiteral("tasks")).toArray()) {
    loaded.taskSelections.append(item.toBool(false));
  }
  if (loaded.taskSelections.size() == 10) {
    loaded.taskSelections.removeAt(3);
  }

  if (loaded.deviceId.isEmpty()
      || !imageFromJson(root.value(QStringLiteral("flash")).toObject(),
                        loaded.flash, error)
      || !imageFromJson(root.value(QStringLiteral("eeprom")).toObject(),
                        loaded.eeprom, error)) {
    if (error.isEmpty()) {
      error = QStringLiteral("Project does not specify a valid MCU.");
    }
    return false;
  }

  project = std::move(loaded);
  error.clear();
  return true;
}

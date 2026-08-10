// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware/Crc16.h"
#include "firmware/FirmwareImage.h"
#include "firmware/IntelHex.h"
#include "firmware/ProjectFile.h"
#include "usb/UsbAspProtocol.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

class FirmwareTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void binaryBounds();
    void sparseIntelHexRoundTrip();
    void checksumFailure();
    void missingEof();
    void trailingRecordAfterEof();
    void extendedSegmentAddress();
    void conflictingOverlap();
    void crossingBoundaryRejected();
    void invalidStartAddressRecord();
    void compareDefinedOnly();
    void highestDefinedAddressTracksSparseEnd();
    void cleanTrailingErasedReadExtent();
    void crc16KnownVector();
    void projectRoundTrip();
    void legacyProjectTaskMigration();
    void legacyProjectClockMigration();
};

void FirmwareTests::binaryBounds()
{
    QString error;
    const QByteArray input = QByteArray::fromHex("00112233A5");
    const FirmwareImage image = FirmwareImage::fromBinary(input, 16, error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(image.capacity(), qsizetype(16));
    QCOMPARE(image.definedCount(), qsizetype(5));
    QCOMPARE(image.toBinary(), input);

    const FirmwareImage invalid = FirmwareImage::fromBinary(input, 4, error);
    Q_UNUSED(invalid);
    QVERIFY(!error.isEmpty());
}

void FirmwareTests::sparseIntelHexRoundTrip()
{
    FirmwareImage source(0x20020, 0xFF);
    source.setByte(0x0010, 0x12, true);
    source.setByte(0x0011, 0x34, true);
    source.setByte(0x10002, 0xA5, true);
    source.setByte(0x20000, 0x5A, true);

    const QByteArray encoded = IntelHex::serialize(source);
    QVERIFY(encoded.contains(":020000040001"));
    QVERIFY(encoded.contains(":020000040002"));

    FirmwareImage decoded(source.capacity(), 0xFF);
    QString error;
    QVERIFY2(IntelHex::parse(encoded, decoded, error), qPrintable(error));
    QCOMPARE(decoded.definedCount(), qsizetype(4));
    QCOMPARE(decoded.byteAt(0x0010), quint8(0x12));
    QCOMPARE(decoded.byteAt(0x0011), quint8(0x34));
    QCOMPARE(decoded.byteAt(0x10002), quint8(0xA5));
    QCOMPARE(decoded.byteAt(0x20000), quint8(0x5A));
    QVERIFY(!decoded.isDefined(0x0012));
}

void FirmwareTests::checksumFailure()
{
    const QByteArray bad = ":0400000001020304F3\r\n:00000001FF\r\n";
    FirmwareImage image(256, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(bad, image, error));
    QVERIFY(error.contains(QStringLiteral("checksum"), Qt::CaseInsensitive));
}


void FirmwareTests::missingEof()
{
    const QByteArray text = ":0100000001FE\r\n";
    FirmwareImage image(256, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(text, image, error));
    QVERIFY(error.contains(QStringLiteral("end-of-file"), Qt::CaseInsensitive));
}

void FirmwareTests::trailingRecordAfterEof()
{
    const QByteArray text = ":00000001FF\r\n:0100000001FE\r\n";
    FirmwareImage image(256, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(text, image, error));
    QVERIFY(error.contains(QStringLiteral("after"), Qt::CaseInsensitive));
}

void FirmwareTests::extendedSegmentAddress()
{
    const QByteArray text = ":020000021000EC\r\n:02001000AABB89\r\n:00000001FF\r\n";
    FirmwareImage image(0x10020, 0xFF);
    QString error;
    QVERIFY2(IntelHex::parse(text, image, error), qPrintable(error));
    QCOMPARE(image.byteAt(0x10010), quint8(0xAA));
    QCOMPARE(image.byteAt(0x10011), quint8(0xBB));
}

void FirmwareTests::conflictingOverlap()
{
    const QByteArray text = ":0100000001FE\r\n:0100000002FD\r\n:00000001FF\r\n";
    FirmwareImage image(256, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(text, image, error));
    QVERIFY(error.contains(QStringLiteral("conflict"), Qt::CaseInsensitive));
}

void FirmwareTests::crossingBoundaryRejected()
{
    const QByteArray text = ":02FFFF000102FD\r\n:00000001FF\r\n";
    FirmwareImage image(0x20000, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(text, image, error));
    QVERIFY(error.contains(QStringLiteral("boundary"), Qt::CaseInsensitive));
}

void FirmwareTests::invalidStartAddressRecord()
{
    const QByteArray text = ":0400010500000000F6\r\n:00000001FF\r\n";
    FirmwareImage image(256, 0xFF);
    QString error;
    QVERIFY(!IntelHex::parse(text, image, error));
    QVERIFY(error.contains(QStringLiteral("start-address"), Qt::CaseInsensitive));
}

void FirmwareTests::compareDefinedOnly()
{
    FirmwareImage image(8, 0xFF);
    image.setByte(2, 0x42, true);
    image.setByte(5, 0xA0, true);
    QByteArray target(8, static_cast<char>(0x00));
    target[2] = static_cast<char>(0x42);
    target[5] = static_cast<char>(0xA0);

    qsizetype mismatch = -1;
    quint8 expected = 0;
    quint8 actual = 0;
    QVERIFY(image.compareDefined(target, mismatch, expected, actual));

    target[5] = static_cast<char>(0xA1);
    QVERIFY(!image.compareDefined(target, mismatch, expected, actual));
    QCOMPARE(mismatch, qsizetype(5));
    QCOMPARE(expected, quint8(0xA0));
    QCOMPARE(actual, quint8(0xA1));
}

void FirmwareTests::highestDefinedAddressTracksSparseEnd()
{
    FirmwareImage image(8192, 0xFF);
    QCOMPARE(image.highestDefinedAddress(), qsizetype(-1));
    image.setByte(0, 0x12, true);
    image.setByte(1023, 0x34, true);
    QCOMPARE(image.highestDefinedAddress(), qsizetype(1023));
    QCOMPARE(image.definedCount(), qsizetype(2));
}

void FirmwareTests::cleanTrailingErasedReadExtent()
{
    FirmwareImage image(256, 0xFF);
    QByteArray bytes(128, static_cast<char>(0xFF));
    bytes[0] = static_cast<char>(0x12);
    bytes[15] = static_cast<char>(0x34);
    bytes[31] = static_cast<char>(0x56);
    image.setBytes(0, bytes, false);
    QCOMPARE(image.cleanTrailingErased(128), qsizetype(32));
    QCOMPARE(image.definedCount(), qsizetype(32));
    QCOMPARE(image.highestDefinedAddress(), qsizetype(31));
    QVERIFY(image.isDefined(20));
    QVERIFY(!image.isDefined(32));

    FirmwareImage blank(64, 0xFF);
    blank.setBytes(0, QByteArray(64, static_cast<char>(0xFF)), false);
    QCOMPARE(blank.cleanTrailingErased(64), qsizetype(0));
    QCOMPARE(blank.definedCount(), qsizetype(0));
}


void FirmwareTests::crc16KnownVector()
{
    QCOMPARE(crc16CcittFalse(QByteArrayView("123456789", 9)), quint16(0x29B1));

    FirmwareImage image(4, 0xFF);
    image.setByte(0, 0x12, true);
    image.setByte(2, 0x34, true);
    const QByteArray expected = QByteArray::fromHex("12FF34FF");
    QCOMPARE(crc16CcittFalse(image), crc16CcittFalse(expected));
}

void FirmwareTests::projectRoundTrip()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FlyingBytesProject source;
    source.deviceId = QStringLiteral("atmega16");
    source.clockId = 10;
    source.flash.reset(32, 0xFF);
    source.eeprom.reset(8, 0xFF);
    source.flash.setByte(0, 0x0C, true);
    source.flash.setByte(31, 0xA5, true);
    source.eeprom.setByte(3, 0x42, true);
    source.preFuses = {0xE1, 0x99, -1};
    source.finalFuses = {0xE1, 0xD9, -1};
    source.lockValue = 0xFC;
    source.taskSelections = {true, true, false, true, false, true, false, false, false};

    const QString path = directory.filePath(QStringLiteral("roundtrip.fbp"));
    QString error;
    QVERIFY2(ProjectFile::save(path, source, error), qPrintable(error));

    FlyingBytesProject loaded;
    QVERIFY2(ProjectFile::load(path, loaded, error), qPrintable(error));
    QCOMPARE(loaded.deviceId, source.deviceId);
    QCOMPARE(loaded.clockId, source.clockId);
    QCOMPARE(loaded.flash.bytes(), source.flash.bytes());
    QCOMPARE(loaded.flash.definedMask(), source.flash.definedMask());
    QCOMPARE(loaded.eeprom.bytes(), source.eeprom.bytes());
    QCOMPARE(loaded.eeprom.definedMask(), source.eeprom.definedMask());
    QCOMPARE(loaded.preFuses, source.preFuses);
    QCOMPARE(loaded.finalFuses, source.finalFuses);
    QCOMPARE(loaded.lockValue, source.lockValue);
    QCOMPARE(loaded.taskSelections, source.taskSelections);
}

void FirmwareTests::legacyProjectTaskMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FlyingBytesProject source;
    source.deviceId = QStringLiteral("atmega16");
    source.clockId = 0;
    source.flash.reset(32, 0xFF);
    source.eeprom.reset(8, 0xFF);
    source.taskSelections = {true, true, false, false, true, false, true, false, false, false};

    const QString path = directory.filePath(QStringLiteral("legacy_tasks.fbp"));
    QString error;
    QVERIFY2(ProjectFile::save(path, source, error), qPrintable(error));

    FlyingBytesProject loaded;
    QVERIFY2(ProjectFile::load(path, loaded, error), qPrintable(error));
    const QVector<bool> expected = {true, true, false, true, false, true, false, false, false};
    QCOMPARE(loaded.taskSelections, expected);
}

void FirmwareTests::legacyProjectClockMigration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    FlyingBytesProject source;
    source.deviceId = QStringLiteral("atmega16");
    source.clockId = static_cast<int>(usbasp::IspClock::Pro);
    source.flash.reset(32, 0xFF);
    source.eeprom.reset(8, 0xFF);
    source.taskSelections = {false, false, false, false, false, false, false, false, false};

    const QString path = directory.filePath(QStringLiteral("legacy_clock.fbp"));
    QString error;
    QVERIFY2(ProjectFile::save(path, source, error), qPrintable(error));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    QVERIFY(document.isObject());
    QJsonObject root = document.object();
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("clockId"), 0);
    document.setObject(root);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QVERIFY(file.write(document.toJson(QJsonDocument::Indented)) > 0);
    file.close();

    FlyingBytesProject loaded;
    QVERIFY2(ProjectFile::load(path, loaded, error), qPrintable(error));
    QCOMPARE(loaded.clockId, static_cast<int>(usbasp::IspClock::Auto));
}

QTEST_APPLESS_MAIN(FirmwareTests)
#include "test_firmware.moc"

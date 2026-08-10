// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class AvrProgrammingInterface {
    SpiIsp,
    Tpi
};

struct AvrDevice
{
    QString id;
    QString name;
    QByteArray signature;
    AvrProgrammingInterface programmingInterface = AvrProgrammingInterface::SpiIsp;

    quint32 flashSize = 0;
    quint16 flashPageSize = 0;
    quint32 eepromSize = 0;
    quint16 eepromPageSize = 0;

    quint16 tpiFlashOffset = 0;
    quint16 tpiSignatureOffset = 0;
    quint16 tpiFuseOffset = 0;
    quint16 tpiLockOffset = 0;

    int flashWriteDelayMs = 10;
    int eepromWriteDelayMs = 10;
    int chipEraseDelayMs = 50;
    int fuseWriteDelayMs = 10;
    int lockWriteDelayMs = 10;
    int resetDelayMs = 100;

    QVector<QByteArray> fuseReadCommands;
    QVector<QByteArray> fuseWriteCommands;
    int fuseReadPosition = 3;
    int fuseWritePosition = 3;
    QVector<quint8> fuseReadMasks;
    QVector<quint8> fuseProgramMasks;
    QVector<int> fuseFactoryValues;

    QByteArray lockReadCommand;
    QByteArray lockWriteCommand;
    int lockReadPosition = 3;
    int lockWritePosition = 3;
    quint8 lockProgramMask = 0;
    int lockFactoryValue = 0xFF;

    bool isValid() const;
    QString signatureText() const;
    int fuseCount() const;
    bool isTpi() const;
    bool hasLockByte() const;

    static AvrDevice fromJson(const QJsonObject& object, QString& error);
    static QByteArray commandFromHex(const QString& text);
};

Q_DECLARE_METATYPE(AvrDevice)

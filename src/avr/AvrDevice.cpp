// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrDevice.h"

#include <QJsonArray>
#include <QStringList>

#include <algorithm>

bool AvrDevice::isValid() const
{
    if (id.isEmpty() || name.isEmpty() || signature.size() != 3
        || flashSize == 0 || flashPageSize == 0
        || flashSize % flashPageSize != 0) {
        return false;
    }
    if (flashWriteDelayMs < 0 || eepromWriteDelayMs < 0
        || chipEraseDelayMs < 0 || fuseWriteDelayMs < 0
        || lockWriteDelayMs < 0 || resetDelayMs < 0) {
        return false;
    }
    if ((eepromSize == 0) != (eepromPageSize == 0)) {
        return false;
    }
    if (eepromPageSize != 0 && eepromSize % eepromPageSize != 0) {
        return false;
    }

    if (isTpi()) {
        if (eepromSize != 0 || eepromPageSize != 0
            || tpiFlashOffset == 0 || tpiSignatureOffset == 0
            || tpiFuseOffset == 0 || tpiLockOffset == 0
            || fuseReadMasks.size() != 1 || fuseProgramMasks.size() != 1
            || (!fuseFactoryValues.isEmpty() && fuseFactoryValues.size() != 1)
            || lockProgramMask == 0) {
            return false;
        }
        if (static_cast<quint32>(tpiFlashOffset) + flashSize > 0x10000u
            || static_cast<quint32>(tpiSignatureOffset) + 3u > 0x10000u) {
            return false;
        }
        if ((fuseProgramMasks.at(0) & static_cast<quint8>(~fuseReadMasks.at(0))) != 0) {
            return false;
        }
        return lockFactoryValue >= 0 && lockFactoryValue <= 0xFF
            && (fuseFactoryValues.isEmpty()
                || (fuseFactoryValues.at(0) >= 0 && fuseFactoryValues.at(0) <= 0xFF));
    }

    if (fuseReadCommands.size() != fuseWriteCommands.size()
        || fuseReadCommands.size() != fuseReadMasks.size()
        || fuseReadCommands.size() != fuseProgramMasks.size()
        || (!fuseFactoryValues.isEmpty() && fuseReadCommands.size() != fuseFactoryValues.size())
        || fuseReadCommands.size() > 3
        || fuseReadPosition < 0 || fuseReadPosition > 3
        || fuseWritePosition < 0 || fuseWritePosition > 3) {
        return false;
    }
    const auto validCommand = [](const QByteArray& command) {
        return command.size() == 4;
    };
    if (!std::all_of(fuseReadCommands.cbegin(), fuseReadCommands.cend(), validCommand)
        || !std::all_of(fuseWriteCommands.cbegin(), fuseWriteCommands.cend(), validCommand)) {
        return false;
    }
    for (qsizetype i = 0; i < fuseProgramMasks.size(); ++i) {
        if ((fuseProgramMasks.at(i) & static_cast<quint8>(~fuseReadMasks.at(i))) != 0) {
            return false;
        }
        if (!fuseFactoryValues.isEmpty()
            && (fuseFactoryValues.at(i) < 0 || fuseFactoryValues.at(i) > 0xFF)) {
            return false;
        }
    }
    if (lockFactoryValue < 0 || lockFactoryValue > 0xFF) {
        return false;
    }
    const bool hasLockRead = lockReadCommand.size() == 4;
    const bool hasLockWrite = lockWriteCommand.size() == 4;
    if (hasLockRead != hasLockWrite
        || (!lockReadCommand.isEmpty() && !hasLockRead)
        || (!lockWriteCommand.isEmpty() && !hasLockWrite)
        || (!hasLockRead && lockProgramMask != 0)
        || lockReadPosition < 0 || lockReadPosition > 3
        || lockWritePosition < 0 || lockWritePosition > 3) {
        return false;
    }
    return true;
}

QString AvrDevice::signatureText() const
{
    QStringList bytes;
    for (const char value : signature) {
        bytes.append(QStringLiteral("%1")
                         .arg(static_cast<quint8>(value), 2, 16, QLatin1Char('0'))
                         .toUpper());
    }
    return bytes.join(QLatin1Char(' '));
}

int AvrDevice::fuseCount() const
{
    if (isTpi()) {
        return tpiFuseOffset != 0 ? 1 : 0;
    }
    return static_cast<int>(std::min(fuseReadCommands.size(), fuseWriteCommands.size()));
}

bool AvrDevice::isTpi() const
{
    return programmingInterface == AvrProgrammingInterface::Tpi;
}

bool AvrDevice::hasLockByte() const
{
    return isTpi() ? tpiLockOffset != 0
                   : lockReadCommand.size() == 4 && lockWriteCommand.size() == 4;
}

AvrDevice AvrDevice::fromJson(const QJsonObject& object, QString& error)
{
    AvrDevice device;
    device.id = object.value(QStringLiteral("id")).toString();
    device.name = object.value(QStringLiteral("name")).toString();
    device.signature = QByteArray::fromHex(
        object.value(QStringLiteral("signature")).toString().toLatin1());
    const QString interfaceName = object.value(QStringLiteral("programmingInterface"))
        .toString(QStringLiteral("spi-isp"));
    device.programmingInterface = interfaceName.compare(QStringLiteral("tpi"), Qt::CaseInsensitive) == 0
        ? AvrProgrammingInterface::Tpi : AvrProgrammingInterface::SpiIsp;

    device.flashSize = static_cast<quint32>(object.value(QStringLiteral("flashSize")).toInteger());
    device.flashPageSize = static_cast<quint16>(object.value(QStringLiteral("flashPageSize")).toInteger());
    device.eepromSize = static_cast<quint32>(object.value(QStringLiteral("eepromSize")).toInteger());
    device.eepromPageSize = static_cast<quint16>(object.value(QStringLiteral("eepromPageSize")).toInteger());
    device.tpiFlashOffset = static_cast<quint16>(object.value(QStringLiteral("tpiFlashOffset")).toInt(0));
    device.tpiSignatureOffset = static_cast<quint16>(object.value(QStringLiteral("tpiSignatureOffset")).toInt(0));
    device.tpiFuseOffset = static_cast<quint16>(object.value(QStringLiteral("tpiFuseOffset")).toInt(0));
    device.tpiLockOffset = static_cast<quint16>(object.value(QStringLiteral("tpiLockOffset")).toInt(0));

    device.flashWriteDelayMs = object.value(QStringLiteral("flashWriteDelayMs")).toInt(10);
    device.eepromWriteDelayMs = object.value(QStringLiteral("eepromWriteDelayMs")).toInt(10);
    device.chipEraseDelayMs = object.value(QStringLiteral("chipEraseDelayMs")).toInt(50);
    device.fuseWriteDelayMs = object.value(QStringLiteral("fuseWriteDelayMs")).toInt(10);
    device.lockWriteDelayMs = object.value(QStringLiteral("lockWriteDelayMs")).toInt(10);
    device.resetDelayMs = object.value(QStringLiteral("resetDelayMs")).toInt(100);

    const auto readCommands = object.value(QStringLiteral("fuseReadCommands")).toArray();
    for (const auto& value : readCommands) {
        const QByteArray command = commandFromHex(value.toString());
        if (command.size() == 4) {
            device.fuseReadCommands.append(command);
        }
    }

    const auto writeCommands = object.value(QStringLiteral("fuseWriteCommands")).toArray();
    for (const auto& value : writeCommands) {
        const QByteArray command = commandFromHex(value.toString());
        if (command.size() == 4) {
            device.fuseWriteCommands.append(command);
        }
    }

    device.fuseReadPosition = object.value(QStringLiteral("fuseReadPosition")).toInt(3);
    device.fuseWritePosition = object.value(QStringLiteral("fuseWritePosition")).toInt(3);

    for (const auto& value : object.value(QStringLiteral("fuseReadMasks")).toArray()) {
        device.fuseReadMasks.append(static_cast<quint8>(value.toInt(0xFF)));
    }
    for (const auto& value : object.value(QStringLiteral("fuseProgramMasks")).toArray()) {
        device.fuseProgramMasks.append(static_cast<quint8>(value.toInt(0)));
    }
    for (const auto& value : object.value(QStringLiteral("fuseFactoryValues")).toArray()) {
        device.fuseFactoryValues.append(value.toInt(0xFF));
    }

    device.lockReadCommand = commandFromHex(object.value(QStringLiteral("lockReadCommand")).toString());
    device.lockWriteCommand = commandFromHex(object.value(QStringLiteral("lockWriteCommand")).toString());
    device.lockReadPosition = object.value(QStringLiteral("lockReadPosition")).toInt(3);
    device.lockWritePosition = object.value(QStringLiteral("lockWritePosition")).toInt(3);
    device.lockProgramMask = static_cast<quint8>(object.value(QStringLiteral("lockProgramMask")).toInt(0));
    device.lockFactoryValue = object.value(QStringLiteral("lockFactoryValue")).toInt(0xFF);

    if (!device.isValid()) {
        error = QStringLiteral("Invalid AVR device entry: %1").arg(device.name);
        return {};
    }

    error.clear();
    return device;
}

QByteArray AvrDevice::commandFromHex(const QString& text)
{
    if (text.trimmed().isEmpty()) {
        return {};
    }
    return QByteArray::fromHex(text.trimmed().toLatin1());
}

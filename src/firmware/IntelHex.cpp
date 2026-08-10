// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware/IntelHex.h"

#include <QFile>
#include <QList>

#include <algorithm>

namespace {

bool parseHexByte(QByteArrayView text, quint8& value)
{
    bool ok = false;
    const uint parsed = QByteArray(text.data(), text.size()).toUInt(&ok, 16);
    if (!ok || parsed > 0xFFu) {
        return false;
    }
    value = static_cast<quint8>(parsed);
    return true;
}

} // namespace

bool IntelHex::parse(QByteArrayView text, FirmwareImage& image, QString& error)
{
    image.clear();
    quint32 baseAddress = 0;
    bool eofSeen = false;

    const QList<QByteArray> lines = QByteArray(text.data(), text.size()).split('\n');
    for (qsizetype lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QByteArray line = lines.at(lineIndex).trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const int humanLine = static_cast<int>(lineIndex + 1);
        if (eofSeen) {
            error = QStringLiteral("Intel HEX line %1 appears after the end-of-file record.").arg(humanLine);
            return false;
        }
        if (!line.startsWith(':')) {
            error = QStringLiteral("Intel HEX line %1 does not begin with ':'.").arg(humanLine);
            return false;
        }
        line.remove(0, 1);
        if ((line.size() % 2) != 0 || line.size() < 10) {
            error = QStringLiteral("Intel HEX line %1 has an invalid length.").arg(humanLine);
            return false;
        }

        QByteArray raw;
        raw.reserve(line.size() / 2);
        for (qsizetype i = 0; i < line.size(); i += 2) {
            quint8 byte = 0;
            if (!parseHexByte(QByteArrayView(line).sliced(i, 2), byte)) {
                error = QStringLiteral("Intel HEX line %1 contains non-hexadecimal data.").arg(humanLine);
                return false;
            }
            raw.append(static_cast<char>(byte));
        }

        const quint8 byteCount = static_cast<quint8>(raw.at(0));
        if (raw.size() != static_cast<qsizetype>(byteCount) + 5) {
            error = QStringLiteral("Intel HEX line %1 byte count does not match its length.").arg(humanLine);
            return false;
        }

        quint8 sum = 0;
        for (const char byte : raw) {
            sum = static_cast<quint8>(sum + static_cast<quint8>(byte));
        }
        if (sum != 0) {
            error = QStringLiteral("Intel HEX checksum failure on line %1.").arg(humanLine);
            return false;
        }

        const quint16 address = (static_cast<quint16>(static_cast<quint8>(raw.at(1))) << 8u)
            | static_cast<quint8>(raw.at(2));
        const quint8 type = static_cast<quint8>(raw.at(3));
        const QByteArrayView payload(raw.constData() + 4, byteCount);

        switch (type) {
        case 0x00: {
            if (static_cast<quint32>(address) + byteCount > 0x10000u) {
                error = QStringLiteral("Intel HEX data record on line %1 crosses a 64 KiB address boundary.").arg(humanLine);
                return false;
            }
            const quint64 absolute = static_cast<quint64>(baseAddress) + address;
            if (absolute + byteCount > static_cast<quint64>(image.capacity())) {
                error = QStringLiteral("Intel HEX data on line %1 exceeds the selected memory capacity at address 0x%2.")
                            .arg(humanLine)
                            .arg(absolute, 8, 16, QLatin1Char('0'));
                return false;
            }
            for (qsizetype i = 0; i < payload.size(); ++i) {
                const qsizetype target = static_cast<qsizetype>(absolute) + i;
                const quint8 incoming = static_cast<quint8>(payload.at(i));
                if (image.isDefined(target) && image.byteAt(target) != incoming) {
                    error = QStringLiteral("Intel HEX line %1 conflicts with an earlier record at address 0x%2.")
                                .arg(humanLine)
                                .arg(target, 8, 16, QLatin1Char('0'));
                    return false;
                }
            }
            image.setBytes(static_cast<qsizetype>(absolute), payload, true);
            break;
        }
        case 0x01:
            if (byteCount != 0 || address != 0) {
                error = QStringLiteral("Invalid end-of-file record on Intel HEX line %1.").arg(humanLine);
                return false;
            }
            eofSeen = true;
            break;
        case 0x02:
            if (byteCount != 2 || address != 0) {
                error = QStringLiteral("Invalid extended-segment record on Intel HEX line %1.").arg(humanLine);
                return false;
            }
            baseAddress = static_cast<quint32>(
                (static_cast<quint16>(static_cast<quint8>(payload.at(0))) << 8u)
                | static_cast<quint8>(payload.at(1))) << 4u;
            break;
        case 0x04:
            if (byteCount != 2 || address != 0) {
                error = QStringLiteral("Invalid extended-linear record on Intel HEX line %1.").arg(humanLine);
                return false;
            }
            baseAddress = static_cast<quint32>(
                (static_cast<quint16>(static_cast<quint8>(payload.at(0))) << 8u)
                | static_cast<quint8>(payload.at(1))) << 16u;
            break;
        case 0x03:
        case 0x05:
            if (byteCount != 4 || address != 0) {
                error = QStringLiteral("Invalid start-address record on Intel HEX line %1.").arg(humanLine);
                return false;
            }
            // Start-address records do not affect programming data.
            break;
        default:
            error = QStringLiteral("Unsupported Intel HEX record type 0x%1 on line %2.")
                        .arg(type, 2, 16, QLatin1Char('0')).arg(humanLine);
            return false;
        }

    }

    if (!eofSeen) {
        error = QStringLiteral("Intel HEX file does not contain an end-of-file record.");
        return false;
    }
    error.clear();
    return true;
}

QByteArray IntelHex::serialize(const FirmwareImage& image)
{
    QByteArray output;
    quint32 currentUpper = 0xFFFFFFFFu;
    qsizetype offset = 0;

    while (offset < image.capacity()) {
        while (offset < image.capacity() && !image.isDefined(offset)) {
            ++offset;
        }
        if (offset >= image.capacity()) {
            break;
        }

        const quint32 upper = static_cast<quint32>(offset) >> 16u;
        if (upper != currentUpper) {
            QByteArray highBytes;
            highBytes.reserve(2);
            highBytes.append(static_cast<char>((upper >> 8u) & 0xFFu));
            highBytes.append(static_cast<char>(upper & 0xFFu));
            output += makeRecord(0x04, 0, highBytes);
            currentUpper = upper;
        }

        const quint16 lowAddress = static_cast<quint16>(offset & 0xFFFF);
        const qsizetype maxBeforeBoundary = 0x10000 - lowAddress;
        QByteArray payload;
        payload.reserve(16);
        while (payload.size() < 16
               && payload.size() < maxBeforeBoundary
               && offset < image.capacity()
               && image.isDefined(offset)
               && (static_cast<quint32>(offset) >> 16u) == currentUpper) {
            payload.append(static_cast<char>(image.byteAt(offset)));
            ++offset;
        }
        output += makeRecord(0x00, lowAddress, payload);
    }

    output += makeRecord(0x01, 0, QByteArrayView{});
    return output;
}

bool IntelHex::loadFile(const QString& path, FirmwareImage& image, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        error = QStringLiteral("Cannot open %1 for reading: %2").arg(path, file.errorString());
        return false;
    }
    return parse(file.readAll(), image, error);
}

bool IntelHex::saveFile(const QString& path, const FirmwareImage& image, QString& error)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        error = QStringLiteral("Cannot open %1 for writing: %2").arg(path, file.errorString());
        return false;
    }
    const QByteArray encoded = serialize(image);
    if (file.write(encoded) != encoded.size()) {
        error = QStringLiteral("Cannot write %1 completely: %2").arg(path, file.errorString());
        return false;
    }
    error.clear();
    return true;
}

QByteArray IntelHex::makeRecord(quint8 type, quint16 address, QByteArrayView data)
{
    QByteArray raw;
    raw.reserve(data.size() + 5);
    raw.append(static_cast<char>(data.size()));
    raw.append(static_cast<char>((address >> 8u) & 0xFFu));
    raw.append(static_cast<char>(address & 0xFFu));
    raw.append(static_cast<char>(type));
    raw.append(data.data(), data.size());

    quint8 sum = 0;
    for (const char byte : raw) {
        sum = static_cast<quint8>(sum + static_cast<quint8>(byte));
    }
    raw.append(static_cast<char>(static_cast<quint8>(0u - sum)));

    return QByteArrayLiteral(":") + raw.toHex().toUpper() + QByteArrayLiteral("\r\n");
}

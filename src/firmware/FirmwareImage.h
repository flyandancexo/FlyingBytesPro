// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QBitArray>
#include <QByteArray>
#include <QByteArrayView>
#include <QMetaType>
#include <QString>

class FirmwareImage
{
public:
    explicit FirmwareImage(qsizetype capacity = 0, quint8 erasedValue = 0xFF);

    void reset(qsizetype capacity, quint8 erasedValue = 0xFF);
    void clear();

    qsizetype capacity() const;
    quint8 erasedValue() const;
    const QByteArray& bytes() const;
    const QBitArray& definedMask() const;

    quint8 byteAt(qsizetype offset) const;
    bool isDefined(qsizetype offset) const;
    void setByte(qsizetype offset, quint8 value, bool defined = true);
    void setBytes(qsizetype offset, QByteArrayView values, bool defined = true);
    void markDefined(qsizetype offset, qsizetype length, bool defined = true);
    void markAllDefined();

    qsizetype definedCount() const;
    qsizetype highestDefinedAddress() const;
    qsizetype cleanTrailingErased(qsizetype scanLength = -1);
    bool anyDefined(qsizetype offset, qsizetype length) const;
    bool anyDefinedNonErased(qsizetype offset, qsizetype length) const;

    QByteArray dataForRange(qsizetype offset, qsizetype length,
                            quint8 undefinedFill = 0xFF) const;

    bool compareDefined(QByteArrayView target, qsizetype& mismatchOffset,
                        quint8& expected, quint8& actual) const;

    static FirmwareImage fromBinary(QByteArrayView binary, qsizetype capacity,
                                    QString& error, quint8 erasedValue = 0xFF);
    QByteArray toBinary() const;

private:
    QByteArray m_bytes;
    QBitArray m_defined;
    quint8 m_erasedValue = 0xFF;
};

Q_DECLARE_METATYPE(FirmwareImage)

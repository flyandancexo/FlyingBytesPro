// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "firmware/FirmwareImage.h"

#include <algorithm>

FirmwareImage::FirmwareImage(qsizetype capacity, quint8 erasedValue)
{
    reset(capacity, erasedValue);
}

void FirmwareImage::reset(qsizetype capacity, quint8 erasedValue)
{
    capacity = std::max<qsizetype>(0, capacity);
    m_erasedValue = erasedValue;
    m_bytes = QByteArray(capacity, static_cast<char>(erasedValue));
    m_defined = QBitArray(capacity, false);
}

void FirmwareImage::clear()
{
    m_bytes.fill(static_cast<char>(m_erasedValue));
    m_defined.fill(false);
}

qsizetype FirmwareImage::capacity() const
{
    return m_bytes.size();
}

quint8 FirmwareImage::erasedValue() const
{
    return m_erasedValue;
}

const QByteArray& FirmwareImage::bytes() const
{
    return m_bytes;
}

const QBitArray& FirmwareImage::definedMask() const
{
    return m_defined;
}

quint8 FirmwareImage::byteAt(qsizetype offset) const
{
    if (offset < 0 || offset >= m_bytes.size()) {
        return m_erasedValue;
    }
    return static_cast<quint8>(m_bytes.at(offset));
}

bool FirmwareImage::isDefined(qsizetype offset) const
{
    return offset >= 0 && offset < m_defined.size() && m_defined.testBit(offset);
}

void FirmwareImage::setByte(qsizetype offset, quint8 value, bool defined)
{
    if (offset < 0 || offset >= m_bytes.size()) {
        return;
    }
    m_bytes[offset] = static_cast<char>(value);
    m_defined.setBit(offset, defined);
}

void FirmwareImage::setBytes(qsizetype offset, QByteArrayView values, bool defined)
{
    if (offset < 0 || offset >= m_bytes.size() || values.isEmpty()) {
        return;
    }
    const qsizetype count = std::min(values.size(), m_bytes.size() - offset);
    std::copy_n(values.data(), count, m_bytes.data() + offset);
    markDefined(offset, count, defined);
}

void FirmwareImage::markDefined(qsizetype offset, qsizetype length, bool defined)
{
    if (length <= 0 || offset < 0 || offset >= m_defined.size()) {
        return;
    }
    const qsizetype end = std::min(offset + length, m_defined.size());
    for (qsizetype i = offset; i < end; ++i) {
        m_defined.setBit(i, defined);
    }
}

void FirmwareImage::markAllDefined()
{
    m_defined.fill(true);
}

qsizetype FirmwareImage::definedCount() const
{
    qsizetype count = 0;
    for (qsizetype i = 0; i < m_defined.size(); ++i) {
        if (m_defined.testBit(i)) {
            ++count;
        }
    }
    return count;
}

qsizetype FirmwareImage::highestDefinedAddress() const
{
    for (qsizetype i = m_defined.size() - 1; i >= 0; --i) {
        if (m_defined.testBit(i)) {
            return i;
        }
    }
    return -1;
}

qsizetype FirmwareImage::cleanTrailingErased(qsizetype scanLength)
{
    const qsizetype limit = scanLength < 0
        ? m_bytes.size()
        : std::clamp(scanLength, qsizetype(0), m_bytes.size());
    qsizetype used = 0;
    for (qsizetype i = limit; i > 0; --i) {
        if (static_cast<quint8>(m_bytes.at(i - 1)) != m_erasedValue) {
            used = i;
            break;
        }
    }
    m_defined.fill(false);
    markDefined(0, used, true);
    return used;
}

bool FirmwareImage::anyDefined(qsizetype offset, qsizetype length) const
{
    if (length <= 0 || offset < 0 || offset >= m_defined.size()) {
        return false;
    }
    const qsizetype end = std::min(offset + length, m_defined.size());
    for (qsizetype i = offset; i < end; ++i) {
        if (m_defined.testBit(i)) {
            return true;
        }
    }
    return false;
}

bool FirmwareImage::anyDefinedNonErased(qsizetype offset, qsizetype length) const
{
    if (length <= 0 || offset < 0 || offset >= m_defined.size()) {
        return false;
    }
    const qsizetype end = std::min(offset + length, m_defined.size());
    for (qsizetype i = offset; i < end; ++i) {
        if (m_defined.testBit(i) && byteAt(i) != m_erasedValue) {
            return true;
        }
    }
    return false;
}

QByteArray FirmwareImage::dataForRange(qsizetype offset, qsizetype length,
                                       quint8 undefinedFill) const
{
    if (length <= 0 || offset < 0 || offset >= m_bytes.size()) {
        return {};
    }
    const qsizetype count = std::min(length, m_bytes.size() - offset);
    QByteArray result(count, static_cast<char>(undefinedFill));
    for (qsizetype i = 0; i < count; ++i) {
        if (m_defined.testBit(offset + i)) {
            result[i] = m_bytes.at(offset + i);
        }
    }
    return result;
}

bool FirmwareImage::compareDefined(QByteArrayView target, qsizetype& mismatchOffset,
                                   quint8& expected, quint8& actual) const
{
    const qsizetype count = std::min(m_bytes.size(), target.size());
    for (qsizetype i = 0; i < count; ++i) {
        if (m_defined.testBit(i)
            && static_cast<quint8>(m_bytes.at(i)) != static_cast<quint8>(target.at(i))) {
            mismatchOffset = i;
            expected = static_cast<quint8>(m_bytes.at(i));
            actual = static_cast<quint8>(target.at(i));
            return false;
        }
    }

    for (qsizetype i = count; i < m_bytes.size(); ++i) {
        if (m_defined.testBit(i)) {
            mismatchOffset = i;
            expected = static_cast<quint8>(m_bytes.at(i));
            actual = 0;
            return false;
        }
    }
    mismatchOffset = -1;
    return true;
}

FirmwareImage FirmwareImage::fromBinary(QByteArrayView binary, qsizetype capacity,
                                        QString& error, quint8 erasedValue)
{
    if (binary.size() > capacity) {
        error = QStringLiteral("Binary image is %1 bytes, exceeding the selected memory capacity of %2 bytes.")
                    .arg(binary.size()).arg(capacity);
        return FirmwareImage(0, erasedValue);
    }

    FirmwareImage image(capacity, erasedValue);
    image.setBytes(0, binary, true);
    error.clear();
    return image;
}

QByteArray FirmwareImage::toBinary() const
{
    const qsizetype last = highestDefinedAddress();
    if (last < 0) {
        return {};
    }
    return dataForRange(0, last + 1, m_erasedValue);
}

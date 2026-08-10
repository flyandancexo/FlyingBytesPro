// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avr/AvrDevice.h"
#include "firmware/FirmwareImage.h"
#include "usb/UsbAspDevice.h"

#include <QVector>

#include <array>
#include <functional>

class AvrIspProgrammer
{
public:
    using ProgressCallback = std::function<bool(qsizetype completed, qsizetype total)>;

    explicit AvrIspProgrammer(UsbAspDevice& usbasp,
                              bool ignoreSignatureMatching = false);
    ~AvrIspProgrammer();

    bool begin(usbasp::IspClock clock, QString& warning, QString& error);
    bool begin(const AvrDevice& device, usbasp::IspClock clock, QString& warning, QString& error);
    bool beginTpi(usbasp::IspClock clock, QString& warning, QString& error);
    void end();
    bool active() const;

    bool readSignature(QByteArray& signature, QString& error);
    bool chipErase(const AvrDevice& device, QString& error);

    bool readFlash(const AvrDevice& device, FirmwareImage& image, bool fullRead,
                   const ProgressCallback& progress, QString& error);
    bool readEeprom(const AvrDevice& device, FirmwareImage& image,
                    const ProgressCallback& progress, QString& error);

    bool writeFlash(const AvrDevice& device, const FirmwareImage& image,
                    bool eraseFirst, const ProgressCallback& progress,
                    QString& error);
    bool writeEeprom(const AvrDevice& device, const FirmwareImage& image,
                     const ProgressCallback& progress, QString& error);

    bool verifyFlash(const AvrDevice& device, const FirmwareImage& image,
                     const ProgressCallback& progress, QString& error);
    bool verifyEeprom(const AvrDevice& device, const FirmwareImage& image,
                      const ProgressCallback& progress, QString& error);

    bool blankCheckFlash(const AvrDevice& device, const ProgressCallback& progress,
                         QString& error);
    bool blankCheckEeprom(const AvrDevice& device, const ProgressCallback& progress,
                          QString& error);

    bool readFuses(const AvrDevice& device, QVector<quint8>& values, QString& error);
    bool writeFuses(const AvrDevice& device, const QVector<int>& requestedValues,
                    QString& error);
    bool readLock(const AvrDevice& device, quint8& value, QString& error);
    bool writeLock(const AvrDevice& device, quint8 value, QString& error);

    quint32 capabilities() const;
    qsizetype lastTransferBytes() const;
    qint64 lastTransferElapsedMs() const;

private:
    bool validateDevice(const AvrDevice& device, QString& error);
    bool reenterProgrammingMode(int resetDelayMs, QString& error);
    bool execute(const QByteArray& command, QByteArray& response, QString& error);
    bool verifyImage(const AvrDevice& device, const FirmwareImage& image,
                     bool flash, const ProgressCallback& progress, QString& error);
    bool blankCheck(const AvrDevice& device, bool flash,
                    const ProgressCallback& progress, QString& error);

    UsbAspDevice& m_usbasp;
    usbasp::IspClock m_clock = usbasp::IspClock::Pro;
    quint32 m_capabilities = 0;
    bool m_active = false;
    bool m_tpiMode = false;
    quint16 m_tpiSignatureOffset = 0;
    bool m_ignoreSignatureMatching = false;
    bool m_refreshSessionBeforeNextDeviceAccess = false;
    qsizetype m_lastTransferBytes = 0;
    qint64 m_lastTransferElapsedMs = 0;
};

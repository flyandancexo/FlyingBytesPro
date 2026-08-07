// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avr/AvrDevice.h"
#include "firmware/FirmwareImage.h"
#include "usb/UsbAspProtocol.h"

#include <QElapsedTimer>
#include <QObject>
#include <QVector>

#include <atomic>

class UsbAspDevice;

class ProgrammerController : public QObject
{
    Q_OBJECT

public:
    explicit ProgrammerController(bool demoMode, QObject* parent = nullptr);

    void requestCancel() noexcept;
    bool demoMode() const noexcept;

public Q_SLOTS:
    void probe(int clockId, bool quiet);
    void detectTarget(int clockId);
    void readFlash(AvrDevice device, int clockId, bool fullRead);
    void readEeprom(AvrDevice device, int clockId);
    void writeFlash(AvrDevice device, FirmwareImage image,
                    bool eraseFirst, bool verifyAfter, int clockId);
    void writeEeprom(AvrDevice device, FirmwareImage image,
                     bool verifyAfter, int clockId);
    void verifyFlash(AvrDevice device, FirmwareImage image, int clockId);
    void verifyEeprom(AvrDevice device, FirmwareImage image, int clockId);
    void eraseChip(AvrDevice device, int clockId);
    void blankCheckFlash(AvrDevice device, int clockId);
    void blankCheckEeprom(AvrDevice device, int clockId);
    void readFuses(AvrDevice device, int clockId);
    void writeFuses(AvrDevice device, QVector<int> values, int clockId);
    void writeLock(AvrDevice device, int value, int clockId);

Q_SIGNALS:
    void logMessage(QString message);
    void progressChanged(int percent);
    void probeFinished(bool success, QString message, quint32 capabilities, bool quiet);
    void signatureFinished(bool success, QString message, QByteArray signature);
    void imageFinished(QString operation, bool success, QString message,
                       FirmwareImage image);
    void operationFinished(QString operation, bool success, QString message);
    void fusesFinished(bool success, QString message, QVector<int> values,
                       int lockValue);

private:
    usbasp::IspClock clockFromId(int id) const;
    bool progress(qsizetype completed, qsizetype total);
    void beginOperation(const QString& name);
    void finishProgress();
    void attachTrace(UsbAspDevice& usb);

    void ensureDemoDevice(const AvrDevice& device);
    bool demoVerify(const FirmwareImage& requested, const FirmwareImage& memory,
                    QString& error) const;
    bool demoBlank(const FirmwareImage& memory, QString& error) const;

    bool m_demoMode = false;
    std::atomic_bool m_cancelRequested = false;
    QString m_demoDeviceId;
    FirmwareImage m_demoFlash;
    FirmwareImage m_demoEeprom;
    QVector<int> m_demoFuses;
    int m_demoLock = 0xFF;
    int m_lastProgressPercent = -1;
    QElapsedTimer m_progressEmitTimer;
};

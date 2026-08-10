// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "usb/UsbAspProtocol.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <array>
#include <functional>

struct libusb_context;
struct libusb_device_handle;

class UsbAspDevice
{
public:
    struct DeviceInfo {
        quint16 vid = 0;
        quint16 pid = 0;
        quint8 bus = 0;
        quint8 address = 0;
        QString manufacturer;
        QString product;
        QString serialNumber;
    };

    UsbAspDevice();
    ~UsbAspDevice();

    UsbAspDevice(const UsbAspDevice&) = delete;
    UsbAspDevice& operator=(const UsbAspDevice&) = delete;

    using TraceCallback = std::function<void(const QString&)>;

    bool open(QString& error);
    void setTraceCallback(TraceCallback callback);
    void setDetailedBulkTracing(bool enabled);
    void close();
    bool isOpen() const;

    const DeviceInfo& info() const;

    bool queryCapabilities(quint32& capabilities, QString& error);
    bool setIspClock(usbasp::IspClock clock, QString& error);
    bool connectTarget(QString& error);
    bool connectTpi(int delayCount, QString& error);
    void disconnectTarget();
    bool enableProgramming(QString& error);
    bool enableTpiProgramming(QString& error);

    bool tpiReadMemory(quint16 address, qsizetype length, QByteArray& output,
                       const std::function<bool(qsizetype, qsizetype)>& progress,
                       QString& error);
    bool tpiWriteMemory(quint16 address, QByteArrayView data,
                        const std::function<bool(qsizetype, qsizetype)>& progress,
                        QString& error);
    bool tpiChipErase(QString& error);
    bool tpiWriteConfigByte(quint16 address, quint8 data, bool eraseSection, QString& error);

    bool transmit(const std::array<quint8, 4>& command,
                  std::array<quint8, 4>& response,
                  QString& error);

    bool readFlash(quint32 address, qsizetype length,
                   usbasp::IspClock clock, QByteArray& output,
                   const std::function<bool(qsizetype, qsizetype)>& progress,
                   QString& error);

    bool readEeprom(quint32 address, qsizetype length,
                    usbasp::IspClock clock, QByteArray& output,
                    const std::function<bool(qsizetype, qsizetype)>& progress,
                    QString& error);

    bool writeFlash(quint32 address, QByteArrayView data, quint16 pageSize,
                    usbasp::IspClock clock,
                    const std::function<bool(qsizetype, qsizetype)>& progress,
                    QString& error);

    bool writeEeprom(quint32 address, QByteArrayView data, quint16 pageSize,
                     usbasp::IspClock clock,
                     const std::function<bool(qsizetype, qsizetype)>& progress,
                     QString& error);

private:
    bool readMemory(quint8 functionId, quint32 address, qsizetype length,
                    usbasp::IspClock clock, QByteArray& output,
                    const std::function<bool(qsizetype, qsizetype)>& progress,
                    QString& error);

    bool writeMemory(quint8 functionId, quint32 address, QByteArrayView data,
                     quint16 pageSize, usbasp::IspClock clock,
                     const std::function<bool(qsizetype, qsizetype)>& progress,
                     QString& error);

    bool setLongAddress(quint32 address, QString& error);
    bool tpiRawWrite(quint8 value, QString& error);
    bool tpiRawRead(quint8& value, QString& error);
    bool tpiNvmWaitBusy(QString& error);

    int controlTransfer(bool deviceToHost, quint8 functionId,
                        const std::array<quint8, 4>& setup,
                        unsigned char* data, quint16 length,
                        unsigned int timeoutMs, QString& error,
                        bool suppressFailureTrace = false);

    QString readStringDescriptor(quint8 descriptorIndex) const;
    static QString libusbErrorText(int code);

    libusb_context* m_context = nullptr;
    libusb_device_handle* m_handle = nullptr;
    DeviceInfo m_info;
    bool m_targetConnected = false;
    bool m_tpiConnected = false;
    bool m_longAddressActive = false;
    TraceCallback m_trace;
    bool m_detailedBulkTracing = false;
};

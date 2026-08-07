// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "usb/UsbAspDevice.h"

#if __has_include(<libusb.h>)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif

#include <QThread>

#include <algorithm>

#include <utility>

namespace {

bool isUsbAspId(const libusb_device_descriptor& descriptor)
{
    return (descriptor.idVendor == usbasp::SharedVid && descriptor.idProduct == usbasp::SharedPid)
        || (descriptor.idVendor == usbasp::OldVid && descriptor.idProduct == usbasp::OldPid);
}

bool productNameLooksLikeUsbAsp(const QString& product)
{
    QString normalized;
    normalized.reserve(product.size());
    for (const QChar ch : product) {
        if (ch.isLetterOrNumber()) {
            normalized.append(ch.toLower());
        }
    }
    return normalized.contains(QStringLiteral("usbasp"));
}

std::array<quint8, 4> addressSetup(quint32 address)
{
    return {
        static_cast<quint8>(address & 0xFFu),
        static_cast<quint8>((address >> 8u) & 0xFFu),
        static_cast<quint8>((address >> 16u) & 0xFFu),
        static_cast<quint8>((address >> 24u) & 0xFFu)
    };
}

unsigned int memoryTransferTimeoutMs(usbasp::IspClock clock, qsizetype bytes) {
  const int frequency = usbasp::clockFrequencyHz(clock);
  if (frequency <= 0 || frequency >= 10'000) {
    return 5000;
  }
  // Slow software-SCK transfers can spend substantially longer inside the
  // USBasp firmware than the raw payload bit count suggests. Use a
  // conservative per-byte clock budget so 500 Hz and 1 kHz transfers do not
  // trip the normal 5 s USB timeout and trigger misleading retry delays.
  const qint64 estimatedMs = (static_cast<qint64>(std::max<qsizetype>(1, bytes))
      * 128 * 1000 + frequency - 1) / frequency;
  return static_cast<unsigned int>(std::clamp<qint64>(estimatedMs + 2000, 5000, 30000));
}

} // namespace

UsbAspDevice::UsbAspDevice() = default;

UsbAspDevice::~UsbAspDevice()
{
    close();
}


void UsbAspDevice::setTraceCallback(TraceCallback callback)
{
    m_trace = std::move(callback);
}

void UsbAspDevice::setDetailedBulkTracing(bool enabled)
{
    m_detailedBulkTracing = enabled;
}

bool UsbAspDevice::open(QString& error)
{
    close();

    int rc = libusb_init(&m_context);
    if (rc < 0) {
        error = QStringLiteral("libusb initialization failed: %1").arg(libusbErrorText(rc));
        m_context = nullptr;
        return false;
    }

#if defined(LIBUSB_OPTION_LOG_LEVEL)
    libusb_set_option(m_context, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
#endif

    libusb_device** devices = nullptr;
    const ssize_t count = libusb_get_device_list(m_context, &devices);
    if (count < 0) {
        error = QStringLiteral("USB device enumeration failed: %1")
                    .arg(libusbErrorText(static_cast<int>(count)));
        close();
        return false;
    }

    QString lastOpenError;
    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        libusb_device_descriptor descriptor{};
        rc = libusb_get_device_descriptor(device, &descriptor);
        if (rc < 0 || !isUsbAspId(descriptor)) {
            continue;
        }

        libusb_device_handle* candidate = nullptr;
        rc = libusb_open(device, &candidate);
        if (rc < 0) {
            lastOpenError = QStringLiteral("USBasp was found but could not be opened: %1")
                                .arg(libusbErrorText(rc));
            continue;
        }

        m_handle = candidate;
        m_info.vid = descriptor.idVendor;
        m_info.pid = descriptor.idProduct;
        m_info.bus = libusb_get_bus_number(device);
        m_info.address = libusb_get_device_address(device);
        m_info.manufacturer = readStringDescriptor(descriptor.iManufacturer);
        m_info.product = readStringDescriptor(descriptor.iProduct);
        m_info.serialNumber = readStringDescriptor(descriptor.iSerialNumber);

        // 16C0:05DC is a shared V-USB VID/PID. Reject a clearly unrelated product,
        // while still accepting clones that omit string descriptors.
        if (descriptor.idVendor == usbasp::SharedVid
            && !m_info.product.isEmpty()
            && !productNameLooksLikeUsbAsp(m_info.product)) {
            libusb_close(m_handle);
            m_handle = nullptr;
            m_info = {};
            continue;
        }
        break;
    }

    libusb_free_device_list(devices, 1);

    if (!m_handle) {
        error = lastOpenError.isEmpty()
            ? QStringLiteral("Programmer not found.")
            : lastOpenError;
        close();
        return false;
    }

    error.clear();
    return true;
}

void UsbAspDevice::close()
{
    if (m_handle) {
        disconnectTarget();
        libusb_close(m_handle);
        m_handle = nullptr;
    }
    if (m_context) {
        libusb_exit(m_context);
        m_context = nullptr;
    }
    m_info = {};
    m_targetConnected = false;
    m_tpiConnected = false;
    m_longAddressActive = false;
}

bool UsbAspDevice::isOpen() const
{
    return m_handle != nullptr;
}

const UsbAspDevice::DeviceInfo& UsbAspDevice::info() const
{
    return m_info;
}

bool UsbAspDevice::queryCapabilities(quint32& capabilities, QString& error)
{
    capabilities = 0;
    std::array<quint8, 4> setup{};
    std::array<unsigned char, 4> response{};
    const int rc = controlTransfer(true, usbasp::FuncGetCapabilities, setup,
                                   response.data(), static_cast<quint16>(response.size()),
                                   1000, error);
    if (rc != 4) {
        if (rc >= 0) {
            error = QStringLiteral("USBasp returned an unexpected capability response length (%1).").arg(rc);
        }
        return false;
    }

    capabilities = static_cast<quint32>(response[0])
        | (static_cast<quint32>(response[1]) << 8u)
        | (static_cast<quint32>(response[2]) << 16u)
        | (static_cast<quint32>(response[3]) << 24u);
    return true;
}

bool UsbAspDevice::setIspClock(usbasp::IspClock clock, QString& error)
{
    if (clock == usbasp::IspClock::Auto) {
        error = QStringLiteral("Automatic SCK must be resolved to a concrete USBasp clock before transmission.");
        return false;
    }
    std::array<quint8, 4> setup{};
    setup[0] = static_cast<quint8>(clock);
    unsigned char response = 0xFF;
    const int rc = controlTransfer(true, usbasp::FuncSetIspSck, setup,
                                   &response, 1, 1000, error);
    if (rc != 1) {
        if (rc >= 0) {
            error = QStringLiteral("USBasp returned an unexpected ISP-clock response length (%1).").arg(rc);
        }
        return false;
    }
    if (response != 0) {
        error = QStringLiteral("USBasp rejected the ISP clock setting (status 0x%1).")
                    .arg(response, 2, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool UsbAspDevice::connectTarget(QString& error)
{
    std::array<quint8, 4> setup{};
    std::array<unsigned char, 4> response{};
    const int rc = controlTransfer(true, usbasp::FuncConnect, setup,
                                   response.data(), static_cast<quint16>(response.size()),
                                   2000, error);
    if (rc < 0) {
        return false;
    }
    m_targetConnected = true;
    return true;
}

bool UsbAspDevice::connectTpi(int delayCount, QString& error)
{
    delayCount = std::clamp(delayCount, 1, 2047);
    std::array<quint8, 4> setup{
        static_cast<quint8>(delayCount & 0xFF),
        static_cast<quint8>((delayCount >> 8) & 0xFF), 0, 0};
    std::array<unsigned char, 4> response{};
    const int rc = controlTransfer(true, usbasp::FuncTpiConnect, setup,
                                   response.data(), static_cast<quint16>(response.size()),
                                   2000, error);
    if (rc < 0) {
        return false;
    }
    m_targetConnected = true;
    m_tpiConnected = true;
    return true;
}

void UsbAspDevice::disconnectTarget()
{
    if (!m_handle || !m_targetConnected) {
        return;
    }
    std::array<quint8, 4> setup{};
    std::array<unsigned char, 4> response{};
    QString ignored;
    controlTransfer(true, m_tpiConnected ? usbasp::FuncTpiDisconnect : usbasp::FuncDisconnect, setup,
                    response.data(), static_cast<quint16>(response.size()),
                    1000, ignored);
    m_targetConnected = false;
    m_tpiConnected = false;
}

bool UsbAspDevice::enableProgramming(QString& error)
{
    std::array<quint8, 4> setup{};
    unsigned char response = 0xFF;
    const int rc = controlTransfer(true, usbasp::FuncEnableProgramming, setup,
                                   &response, 1, 2000, error);
    if (rc != 1) {
        if (rc >= 0) {
            error = QStringLiteral("Target MCU not detected.");
        }
        return false;
    }
    if (response != 0) {
        error = QStringLiteral("Target MCU not detected.");
        return false;
    }
    return true;
}

bool UsbAspDevice::tpiRawWrite(quint8 value, QString& error)
{
    std::array<quint8, 4> setup{value, 0, 0, 0};
    std::array<unsigned char, 4> response{};
    const int rc = controlTransfer(true, usbasp::FuncTpiRawWrite, setup,
                                   response.data(), static_cast<quint16>(response.size()),
                                   2000, error);
    return rc >= 0;
}

bool UsbAspDevice::tpiRawRead(quint8& value, QString& error)
{
    std::array<quint8, 4> setup{};
    std::array<unsigned char, 4> response{};
    const int rc = controlTransfer(true, usbasp::FuncTpiRawRead, setup,
                                   response.data(), static_cast<quint16>(response.size()),
                                   2000, error);
    if (rc != 1) {
        if (rc >= 0) error = QStringLiteral("USBasp returned an unexpected TPI byte count (%1).").arg(rc);
        return false;
    }
    value = response[0];
    return true;
}

bool UsbAspDevice::tpiNvmWaitBusy(QString& error)
{
    for (int retry = 0; retry < 50; ++retry) {
        if (!tpiRawWrite(usbasp::TpiOpSin(usbasp::TpiNvmCsr), error)) return false;
        quint8 status = 0;
        if (!tpiRawRead(status, error)) return false;
        if ((status & usbasp::TpiNvmBusy) == 0) return true;
    }
    error = QStringLiteral("TPI NVM controller remained busy.");
    return false;
}

bool UsbAspDevice::enableTpiProgramming(QString& error)
{
    if (!tpiRawWrite(usbasp::TpiOpSstCs(usbasp::TpiPcr), error)
        || !tpiRawWrite(usbasp::TpiPcrGuard2Bit, error)) return false;
    static constexpr quint8 key[] = {0xE0, 0xFF, 0x88, 0xD8, 0xCD, 0x45, 0xAB, 0x89, 0x12};
    for (quint8 byte : key) if (!tpiRawWrite(byte, error)) return false;
    for (int retry = 0; retry < 10; ++retry) {
        if (!tpiRawWrite(usbasp::TpiOpSldCs(usbasp::TpiIr), error)) return false;
        quint8 ident = 0;
        if (!tpiRawRead(ident, error)) return false;
        if (ident != 0x80) continue;
        if (!tpiRawWrite(usbasp::TpiOpSldCs(usbasp::TpiSr), error)) return false;
        quint8 status = 0;
        if (!tpiRawRead(status, error)) return false;
        if ((status & usbasp::TpiSrNvmEnable) != 0) return true;
    }
    error = QStringLiteral("Target MCU not detected.");
    return false;
}

bool UsbAspDevice::tpiReadMemory(quint16 address, qsizetype length, QByteArray& output,
                                 const std::function<bool(qsizetype, qsizetype)>& progress,
                                 QString& error)
{
    if (length < 0 || static_cast<quint32>(address) + static_cast<quint32>(length) > 0x10000u) {
        error = QStringLiteral("TPI read range is outside the 16-bit TPI data space.");
        return false;
    }
    output.resize(length);
    qsizetype done = 0;
    while (done < length) {
        if (progress && !progress(done, length)) { error = QStringLiteral("Operation cancelled."); return false; }
        const qsizetype block = std::min<qsizetype>(usbasp::TpiBlockSize, length - done);
        const quint16 current = static_cast<quint16>(address + done);
        std::array<quint8, 4> setup{static_cast<quint8>(current & 0xFF), static_cast<quint8>(current >> 8), 0, 0};
        auto* dest = reinterpret_cast<unsigned char*>(output.data() + done);
        const int rc = controlTransfer(true, usbasp::FuncTpiReadBlock, setup, dest,
                                       static_cast<quint16>(block), 5000, error);
        if (rc != static_cast<int>(block)) {
            if (rc >= 0) error = QStringLiteral("USBasp returned %1 TPI bytes while %2 were requested.").arg(rc).arg(block);
            return false;
        }
        done += block;
    }
    if (progress) progress(length, length);
    return true;
}

bool UsbAspDevice::tpiWriteMemory(quint16 address, QByteArrayView data,
                                  const std::function<bool(qsizetype, qsizetype)>& progress,
                                  QString& error)
{
    if (static_cast<quint32>(address) + static_cast<quint32>(data.size()) > 0x10000u) {
        error = QStringLiteral("TPI write range is outside the 16-bit TPI data space.");
        return false;
    }
    qsizetype done = 0;
    while (done < data.size()) {
        if (progress && !progress(done, data.size())) { error = QStringLiteral("Operation cancelled."); return false; }
        const qsizetype block = std::min<qsizetype>(usbasp::TpiBlockSize, data.size() - done);
        const quint16 current = static_cast<quint16>(address + done);
        std::array<quint8, 4> setup{static_cast<quint8>(current & 0xFF), static_cast<quint8>(current >> 8), 0, 0};
        auto* src = reinterpret_cast<unsigned char*>(const_cast<char*>(data.data() + done));
        const int rc = controlTransfer(false, usbasp::FuncTpiWriteBlock, setup, src,
                                       static_cast<quint16>(block), 5000, error);
        if (rc != static_cast<int>(block)) {
            if (rc >= 0) error = QStringLiteral("USBasp accepted %1 TPI bytes while %2 were sent.").arg(rc).arg(block);
            return false;
        }
        done += block;
    }
    if (progress) progress(data.size(), data.size());
    return true;
}

bool UsbAspDevice::tpiChipErase(QString& error)
{
    if (!tpiRawWrite(usbasp::TpiOpSstPr(0), error) || !tpiRawWrite(0x01, error)
        || !tpiRawWrite(usbasp::TpiOpSstPr(1), error) || !tpiRawWrite(0x40, error)
        || !tpiRawWrite(usbasp::TpiOpSout(usbasp::TpiNvmCmd), error)
        || !tpiRawWrite(usbasp::TpiNvmChipErase, error)
        || !tpiRawWrite(usbasp::TpiOpSstInc, error) || !tpiRawWrite(0x00, error)) return false;
    return tpiNvmWaitBusy(error);
}

bool UsbAspDevice::tpiWriteConfigByte(quint16 address, quint8 data, bool eraseSection, QString& error)
{
    if (!tpiNvmWaitBusy(error)) return false;
    if (eraseSection) {
        if (!tpiRawWrite(usbasp::TpiOpSstPr(0), error)
            || !tpiRawWrite(static_cast<quint8>((address & 0xFF) | 1), error)
            || !tpiRawWrite(usbasp::TpiOpSstPr(1), error)
            || !tpiRawWrite(static_cast<quint8>(address >> 8), error)
            || !tpiRawWrite(usbasp::TpiOpSout(usbasp::TpiNvmCmd), error)
            || !tpiRawWrite(usbasp::TpiNvmSectionErase, error)
            || !tpiRawWrite(usbasp::TpiOpSstInc, error) || !tpiRawWrite(0x00, error)
            || !tpiNvmWaitBusy(error)) return false;
    }
    if (!tpiRawWrite(usbasp::TpiOpSstPr(0), error)
        || !tpiRawWrite(static_cast<quint8>(address & 0xFF), error)
        || !tpiRawWrite(usbasp::TpiOpSstPr(1), error)
        || !tpiRawWrite(static_cast<quint8>(address >> 8), error)
        || !tpiRawWrite(usbasp::TpiOpSout(usbasp::TpiNvmCmd), error)
        || !tpiRawWrite(usbasp::TpiNvmWordWrite, error)
        || !tpiRawWrite(usbasp::TpiOpSstInc, error) || !tpiRawWrite(data, error)
        || !tpiRawWrite(usbasp::TpiOpSstInc, error) || !tpiRawWrite(0xFF, error)) return false;
    return tpiNvmWaitBusy(error);
}

bool UsbAspDevice::transmit(const std::array<quint8, 4>& command,
                            std::array<quint8, 4>& response,
                            QString& error)
{
    std::array<unsigned char, 4> raw{};
    const int rc = controlTransfer(true, usbasp::FuncTransmit, command,
                                   raw.data(), static_cast<quint16>(raw.size()),
                                   2000, error);
    if (rc != 4) {
        if (rc >= 0) {
            error = QStringLiteral("USBasp returned an unexpected SPI response length (%1).").arg(rc);
        }
        return false;
    }
    std::copy(raw.begin(), raw.end(), response.begin());
    if (m_trace) {
        QByteArray commandBytes;
        QByteArray responseBytes;
        for (const quint8 byte : command) commandBytes.append(static_cast<char>(byte));
        for (const quint8 byte : response) responseBytes.append(static_cast<char>(byte));
        m_trace(QStringLiteral("ISP CMD %1 -> %2")
            .arg(QString::fromLatin1(commandBytes.toHex(' ').toUpper()),
                 QString::fromLatin1(responseBytes.toHex(' ').toUpper())));
    }
    return true;
}

bool UsbAspDevice::readFlash(quint32 address, qsizetype length,
                             usbasp::IspClock clock, QByteArray& output,
                             const std::function<bool(qsizetype, qsizetype)>& progress,
                             QString& error)
{
    return readMemory(usbasp::FuncReadFlash, address, length, clock, output, progress, error);
}

bool UsbAspDevice::readEeprom(quint32 address, qsizetype length,
                              usbasp::IspClock clock, QByteArray& output,
                              const std::function<bool(qsizetype, qsizetype)>& progress,
                              QString& error)
{
    return readMemory(usbasp::FuncReadEeprom, address, length, clock, output, progress, error);
}

bool UsbAspDevice::writeFlash(quint32 address, QByteArrayView data, quint16 pageSize,
                              usbasp::IspClock clock,
                              const std::function<bool(qsizetype, qsizetype)>& progress,
                              QString& error)
{
    return writeMemory(usbasp::FuncWriteFlash, address, data, pageSize, clock, progress, error);
}

bool UsbAspDevice::writeEeprom(quint32 address, QByteArrayView data, quint16 pageSize,
                               usbasp::IspClock clock,
                               const std::function<bool(qsizetype, qsizetype)>& progress,
                               QString& error)
{
    return writeMemory(usbasp::FuncWriteEeprom, address, data, pageSize, clock, progress, error);
}

bool UsbAspDevice::readMemory(quint8 functionId, quint32 address, qsizetype length,
                              usbasp::IspClock clock, QByteArray& output,
                              const std::function<bool(qsizetype, qsizetype)>& progress,
                              QString& error)
{
    if (length < 0) {
        error = QStringLiteral("Invalid negative read length.");
        return false;
    }

    output.resize(length);
    const int frequency = usbasp::clockFrequencyHz(clock);
    const qsizetype maximumBlock = (frequency > 0 && frequency < 10'000)
        ? usbasp::ReadBlockSize / 10
        : usbasp::ReadBlockSize;

    if (m_trace && !m_detailedBulkTracing) {
        m_trace(QStringLiteral("USBasp bulk read started: function=%1 address=0x%2 bytes=%3 block=%4.")
            .arg(functionId)
            .arg(address, 8, 16, QLatin1Char('0'))
            .arg(length)
            .arg(maximumBlock)
            .toUpper());
    }

    qsizetype done = 0;
    qsizetype transferCount = 0;
    while (done < length) {
        if (progress && !progress(done, length)) {
            error = QStringLiteral("Operation cancelled.");
            return false;
        }

        const quint32 currentAddress = address + static_cast<quint32>(done);
        const qsizetype bytesToBoundary = static_cast<qsizetype>(
            0x10000u - (currentAddress & 0xFFFFu));
        const qsizetype block = std::min({maximumBlock, length - done, bytesToBoundary});
        // Use the standard 16-bit request address below 64 KiB. Extended
        // address setup is sent only when the transfer actually requires it.
        if (!setLongAddress(currentAddress, error)) {
            return false;
        }

        std::array<quint8, 4> setup{
            static_cast<quint8>(currentAddress & 0xFFu),
            static_cast<quint8>((currentAddress >> 8u) & 0xFFu),
            0,
            0
        };
        auto* destination = reinterpret_cast<unsigned char*>(output.data() + done);
        const int rc = controlTransfer(true, functionId, setup, destination,
                                       static_cast<quint16>(block),
                                       memoryTransferTimeoutMs(clock, block), error);
        if (rc != static_cast<int>(block)) {
            if (rc >= 0) {
                error = QStringLiteral("USBasp returned %1 bytes while %2 were requested.")
                            .arg(rc).arg(block);
            }
            return false;
        }
        done += block;
        ++transferCount;
    }

    if (progress) {
        progress(length, length);
    }
    if (m_trace && !m_detailedBulkTracing) {
        m_trace(QStringLiteral("USBasp bulk read completed: %1 byte(s) in %2 control transfer(s).")
            .arg(length).arg(transferCount));
    }
    return true;
}

bool UsbAspDevice::writeMemory(quint8 functionId, quint32 address, QByteArrayView data,
                               quint16 pageSize, usbasp::IspClock clock,
                               const std::function<bool(qsizetype, qsizetype)>& progress,
                               QString& error)
{
    if (pageSize == 0 || pageSize > 0x0FFFu) {
        error = QStringLiteral("Unsupported memory page size: %1 bytes.").arg(pageSize);
        return false;
    }

    const int frequency = usbasp::clockFrequencyHz(clock);
    const qsizetype protocolMaximumBlock = (frequency > 0 && frequency < 10'000)
        ? usbasp::WriteBlockSize / 10
        : usbasp::WriteBlockSize;
    // Keep EEPROM writes inside one device page. Cap Flash writes at 128
    // bytes so a 256-byte USBasp firmware queue retains enough headroom for
    // V-USB packet flow control while target Flash pages are being committed.
    const bool isolateEepromPages = functionId == usbasp::FuncWriteEeprom;
    const bool queueSafeFlashBlocks = functionId == usbasp::FuncWriteFlash;
    const qsizetype maximumBlock = isolateEepromPages
        ? std::min<qsizetype>(protocolMaximumBlock, pageSize)
        : queueSafeFlashBlocks
            ? std::min<qsizetype>(protocolMaximumBlock,
                                  usbasp::QueueSafeFlashWriteBlockSize)
            : protocolMaximumBlock;

    if (m_trace && !m_detailedBulkTracing) {
        m_trace(QStringLiteral("USBasp bulk write started: function=%1 address=0x%2 bytes=%3 block=%4 page=%5.")
            .arg(functionId)
            .arg(address, 8, 16, QLatin1Char('0'))
            .arg(data.size())
            .arg(maximumBlock)
            .arg(pageSize)
            .toUpper());
    }

    qsizetype done = 0;
    qsizetype transferCount = 0;
    quint8 blockFlags = usbasp::BlockFlagFirst;
    while (done < data.size()) {
        if (progress && !progress(done, data.size())) {
            error = QStringLiteral("Operation cancelled.");
            return false;
        }

        const quint32 currentAddress = address + static_cast<quint32>(done);
        const qsizetype bytesToBoundary = static_cast<qsizetype>(
            0x10000u - (currentAddress & 0xFFFFu));
        const qsizetype bytesToPageBoundary = isolateEepromPages
            ? static_cast<qsizetype>(pageSize - (currentAddress % pageSize))
            : data.size() - done;
        const qsizetype block = std::min({maximumBlock, data.size() - done,
                                          bytesToBoundary, bytesToPageBoundary});
        // Use the standard 16-bit request address below 64 KiB. Extended
        // address setup is sent only when the transfer actually requires it.
        if (!setLongAddress(currentAddress, error)) {
            return false;
        }

        std::array<quint8, 4> setup{
            static_cast<quint8>(currentAddress & 0xFFu),
            static_cast<quint8>((currentAddress >> 8u) & 0xFFu),
            static_cast<quint8>(pageSize & 0xFFu),
            static_cast<quint8>((blockFlags & 0x0Fu) | ((pageSize & 0x0F00u) >> 4u))
        };
        blockFlags = isolateEepromPages ? usbasp::BlockFlagFirst : 0;

        auto* source = reinterpret_cast<unsigned char*>(
            const_cast<char*>(data.data() + done));
        const int rc = controlTransfer(false, functionId, setup, source,
                                       static_cast<quint16>(block),
                                       memoryTransferTimeoutMs(clock, block), error);
        if (rc != static_cast<int>(block)) {
            if (rc >= 0) {
                error = QStringLiteral("USBasp accepted %1 bytes while %2 were sent.")
                            .arg(rc).arg(block);
            }
            return false;
        }
        done += block;
        ++transferCount;
    }

    if (progress) {
        progress(data.size(), data.size());
    }
    if (m_trace && !m_detailedBulkTracing) {
        m_trace(QStringLiteral("USBasp bulk write completed: %1 byte(s) in %2 control transfer(s).")
            .arg(data.size()).arg(transferCount));
    }
    return true;
}

bool UsbAspDevice::setLongAddress(quint32 address, QString& error)
{
  const bool requiresLongAddress = address >= 0x10000u;

  // Low-address transfers use the standard 16-bit address fields directly.
  // Do not probe SETLONGADDRESS below 64 KiB: some custom USBasp firmware
  // stalls its control endpoint when that optional request is sent.
  if (!requiresLongAddress && !m_longAddressActive) {
    error.clear();
    return true;
  }

  // If a previous transfer used an extended address, explicitly return the
  // programmer to bank zero before a later low-address operation.
  const quint32 requestedAddress = requiresLongAddress ? address : 0u;
  const auto setup = addressSetup(requestedAddress);
  std::array<unsigned char, 4> response{};
  QString transferError;
  const int rc = controlTransfer(true, usbasp::FuncSetLongAddress, setup,
                                 response.data(), static_cast<quint16>(response.size()),
                                 1000, transferError, true);
  if (rc < 0) {
    error = requiresLongAddress
      ? QStringLiteral("The connected USBasp firmware does not support long addresses required at 0x%1: %2")
          .arg(address, 8, 16, QLatin1Char('0'))
          .arg(transferError)
      : QStringLiteral("USBasp could not return to the low-address bank: %1")
          .arg(transferError);
    return false;
  }

  m_longAddressActive = requiresLongAddress;
  error.clear();
  return true;
}

int UsbAspDevice::controlTransfer(bool deviceToHost, quint8 functionId,
                                  const std::array<quint8, 4>& setup,
                                  unsigned char* data, quint16 length,
                                  unsigned int timeoutMs, QString& error,
                                  bool suppressFailureTrace)
{
    if (!m_handle) {
        error = QStringLiteral("USBasp is not open.");
        return LIBUSB_ERROR_NO_DEVICE;
    }

    const unsigned int direction = deviceToHost
        ? static_cast<unsigned int>(LIBUSB_ENDPOINT_IN)
        : static_cast<unsigned int>(LIBUSB_ENDPOINT_OUT);
    const unsigned int requestBits =
        static_cast<unsigned int>(LIBUSB_REQUEST_TYPE_VENDOR)
        | static_cast<unsigned int>(LIBUSB_RECIPIENT_DEVICE)
        | direction;
    const quint8 requestType = static_cast<quint8>(requestBits);
    const quint16 value = static_cast<quint16>(setup[0])
        | (static_cast<quint16>(setup[1]) << 8u);
    const quint16 index = static_cast<quint16>(setup[2])
        | (static_cast<quint16>(setup[3]) << 8u);

    const bool bulkFunction = functionId == usbasp::FuncReadFlash
        || functionId == usbasp::FuncWriteFlash
        || functionId == usbasp::FuncReadEeprom
        || functionId == usbasp::FuncWriteEeprom
        || functionId == usbasp::FuncTpiReadBlock
        || functionId == usbasp::FuncTpiWriteBlock
        || functionId == usbasp::FuncSetLongAddress;
    const bool traceSuccessfulTransfer = m_trace && (!bulkFunction || m_detailedBulkTracing);

    int rc = LIBUSB_ERROR_OTHER;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        if (traceSuccessfulTransfer) {
            m_trace(QStringLiteral("USB CTRL %1 request=%2 value=0x%3 index=0x%4 length=%5 attempt=%6")
                .arg(deviceToHost ? QStringLiteral("IN") : QStringLiteral("OUT"))
                .arg(functionId)
                .arg(value, 4, 16, QLatin1Char('0'))
                .arg(index, 4, 16, QLatin1Char('0'))
                .arg(length)
                .arg(attempt).toUpper());
        }

        rc = libusb_control_transfer(m_handle, requestType, functionId,
                                     value, index, data, length, timeoutMs);
        if (rc >= 0) {
            if (traceSuccessfulTransfer) {
                m_trace(QStringLiteral("USB CTRL request=%1 completed: %2 byte(s)")
                    .arg(functionId).arg(rc));
            }
            error.clear();
            return rc;
        }

        const bool retryable = rc == LIBUSB_ERROR_IO
            || rc == LIBUSB_ERROR_TIMEOUT
            || rc == LIBUSB_ERROR_INTERRUPTED;
        if (!retryable || attempt == 3) {
            break;
        }
        if (m_trace && !suppressFailureTrace) {
            m_trace(QStringLiteral("USB CTRL request=%1 failed with %2; retrying.")
                .arg(functionId).arg(libusbErrorText(rc)));
        }
        QThread::msleep(static_cast<unsigned long>(attempt * 25));
    }

    error = QStringLiteral("USB control transfer %1 failed after retries: %2")
        .arg(functionId)
        .arg(libusbErrorText(rc));
    if (m_trace && !suppressFailureTrace) {
        m_trace(error);
    }
    return rc;
}

QString UsbAspDevice::readStringDescriptor(quint8 descriptorIndex) const
{
    if (!m_handle || descriptorIndex == 0) {
        return {};
    }
    std::array<unsigned char, 256> buffer{};
    const int rc = libusb_get_string_descriptor_ascii(m_handle, descriptorIndex,
                                                       buffer.data(),
                                                       static_cast<int>(buffer.size()));
    if (rc <= 0) {
        return {};
    }
    return QString::fromUtf8(reinterpret_cast<const char*>(buffer.data()), rc);
}

QString UsbAspDevice::libusbErrorText(int code)
{
    const char* name = libusb_error_name(code);
    return name ? QString::fromLatin1(name) : QStringLiteral("libusb error %1").arg(code);
}

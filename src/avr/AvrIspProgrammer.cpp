// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrIspProgrammer.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <utility>
#include <vector>

namespace {

QByteArray makeIspCommand(quint8 b0, quint8 b1, quint8 b2, quint8 b3)
{
    QByteArray command(4, Qt::Uninitialized);
    command[0] = static_cast<char>(b0);
    command[1] = static_cast<char>(b1);
    command[2] = static_cast<char>(b2);
    command[3] = static_cast<char>(b3);
    return command;
}

bool allErased(QByteArrayView bytes, quint8 erasedValue = 0xFF)
{
    return std::all_of(bytes.begin(), bytes.end(), [erasedValue](char value) {
        return static_cast<quint8>(value) == erasedValue;
    });
}

struct MemoryRun {
  quint32 address = 0;
  QByteArray data;
};

void appendRunPage(std::vector<MemoryRun>& runs, quint32 address, QByteArray pageData) {
  if (!runs.empty()
      && runs.back().address + static_cast<quint32>(runs.back().data.size()) == address) {
    runs.back().data.append(pageData);
    return;
  }
  runs.push_back(MemoryRun{address, std::move(pageData)});
}

qsizetype runBytes(const std::vector<MemoryRun>& runs) {
  qsizetype total = 0;
  for (const MemoryRun& run : runs) {
    total += run.data.size();
  }
  return total;
}

bool pageFullyDefined(const FirmwareImage& image, qsizetype offset, qsizetype length) {
  for (qsizetype i = 0; i < length; ++i) {
    if (!image.isDefined(offset + i)) {
      return false;
    }
  }
  return true;
}


int tpiDelayForClock(usbasp::IspClock clock) {
  const int frequency = usbasp::clockFrequencyHz(clock);
  if (clock == usbasp::IspClock::Pro || frequency <= 0) return 1;
  return std::clamp(1'500'000 / frequency, 1, 2047);
}

QString clockText(usbasp::IspClock clock) {
  const int frequency = usbasp::clockFrequencyHz(clock);
  if (frequency >= 1'000'000) return QStringLiteral("%1 MHz").arg(frequency / 1'000'000.0, 0, 'f', 1);
  if (frequency >= 1'000) return QStringLiteral("%1 kHz").arg(frequency / 1'000.0, 0, 'f', frequency % 1000 ? 2 : 0);
  return QStringLiteral("%1 Hz").arg(frequency);
}

} // namespace

AvrIspProgrammer::AvrIspProgrammer(UsbAspDevice& usbasp,
                                         bool ignoreSignatureMatching)
  : m_usbasp(usbasp),
    m_ignoreSignatureMatching(ignoreSignatureMatching)
{
}

AvrIspProgrammer::~AvrIspProgrammer()
{
    end();
}

bool AvrIspProgrammer::begin(usbasp::IspClock clock, QString& warning, QString& error) {
  end();
  warning.clear();
  if (!m_usbasp.isOpen()) {
    error = QStringLiteral("USBasp must be opened before beginning an ISP session.");
    return false;
  }

  m_capabilities = 0;
  QString capabilitiesError;
  const bool capabilitiesKnown = m_usbasp.queryCapabilities(m_capabilities, capabilitiesError);
  if (!capabilitiesKnown) {
    warning = QStringLiteral("Capability query was not supported: %1").arg(capabilitiesError);
  }

  if (clock == usbasp::IspClock::Pro) {
    QString clockError;
    if (!m_usbasp.setIspClock(usbasp::IspClock::Pro, clockError)) {
      const QString proWarning = QStringLiteral(
        "Pro SCK request was not accepted; continuing with the USBasp firmware default. %1")
        .arg(clockError);
      warning = warning.isEmpty() ? proWarning
                                  : warning + QLatin1Char('\n') + proWarning;
    }
    if (!m_usbasp.connectTarget(error)) {
      return false;
    }
    QThread::msleep(100);
    if (!m_usbasp.enableProgramming(error)) {
      m_usbasp.disconnectTarget();
      return false;
    }
    m_clock = usbasp::IspClock::Pro;
    m_active = true;
    const QString proMessage = QStringLiteral("Pro SCK using USBasp default.");
    warning = warning.isEmpty() ? proMessage : warning + QLatin1Char('\n') + proMessage;
    error.clear();
    return true;
  }

  if (clock != usbasp::IspClock::Auto) {
    if (!m_usbasp.setIspClock(clock, error)) {
      return false;
    }
    if (!m_usbasp.connectTarget(error)) {
      return false;
    }
    QThread::msleep(100);
    if (!m_usbasp.enableProgramming(error)) {
      m_usbasp.disconnectTarget();
      return false;
    }
    m_clock = clock;
    m_active = true;
    error.clear();
    return true;
  }

  QVector<usbasp::IspClock> attempts;
  if (!capabilitiesKnown || (m_capabilities & usbasp::Capability3MHz) != 0) {
    attempts.append(usbasp::IspClock::MHz3);
  }
  attempts.append(usbasp::IspClock::MHz1_5);
  attempts.append(usbasp::IspClock::KHz750);
  attempts.append(usbasp::IspClock::KHz375);
  attempts.append(usbasp::IspClock::KHz187_5);
  attempts.append(usbasp::IspClock::KHz93_75);
  attempts.append(usbasp::IspClock::KHz32);
  attempts.append(usbasp::IspClock::KHz16);
  attempts.append(usbasp::IspClock::KHz8);
  attempts.append(usbasp::IspClock::KHz4);
  attempts.append(usbasp::IspClock::KHz2);
  attempts.append(usbasp::IspClock::KHz1);
  attempts.append(usbasp::IspClock::Hz500);

  QString lastError;
  for (const usbasp::IspClock attempt : attempts) {
    QString clockError;
    if (!m_usbasp.setIspClock(attempt, clockError)) {
      lastError = clockError;
      continue;
    }
    if (!m_usbasp.connectTarget(lastError)) {
      continue;
    }
    QThread::msleep(100);
    if (m_usbasp.enableProgramming(lastError)) {
      m_clock = attempt;
      m_active = true;
      const int frequency = usbasp::clockFrequencyHz(attempt);
      const QString selected = frequency >= 1'000'000
        ? QStringLiteral("%1 MHz").arg(frequency / 1'000'000.0, 0, 'f', 1)
        : frequency >= 1'000
          ? QStringLiteral("%1 kHz").arg(frequency / 1'000.0, 0, 'f', frequency % 1000 ? 2 : 0)
          : QStringLiteral("%1 Hz").arg(frequency);
      const QString autoMessage = QStringLiteral("Auto SCK selected %1.").arg(selected);
      warning = warning.isEmpty() ? autoMessage : warning + QLatin1Char('\n') + autoMessage;
      error.clear();
      return true;
    }
    m_usbasp.disconnectTarget();
  }

  error = lastError.isEmpty()
    ? QStringLiteral("Target MCU not detected at any automatic SCK setting.")
    : lastError;
  return false;
}

bool AvrIspProgrammer::begin(const AvrDevice& device, usbasp::IspClock clock,
                              QString& warning, QString& error) {
  if (device.isTpi()) {
    const bool started = beginTpi(clock, warning, error);
    if (started) m_tpiSignatureOffset = device.tpiSignatureOffset;
    return started;
  }
  m_tpiSignatureOffset = 0;
  return begin(clock, warning, error);
}

bool AvrIspProgrammer::beginTpi(usbasp::IspClock clock, QString& warning, QString& error) {
  end();
  warning.clear();
  if (!m_usbasp.isOpen()) {
    error = QStringLiteral("USBasp must be opened before beginning a TPI session.");
    return false;
  }
  m_capabilities = 0;
  if (!m_usbasp.queryCapabilities(m_capabilities, error)
      || (m_capabilities & usbasp::CapabilityTpi) == 0) {
    error = QStringLiteral("The connected USBasp firmware does not report TPI support.");
    return false;
  }

  QVector<usbasp::IspClock> attempts;
  if (clock == usbasp::IspClock::Auto) {
    attempts = {usbasp::IspClock::MHz3, usbasp::IspClock::MHz1_5, usbasp::IspClock::KHz750,
      usbasp::IspClock::KHz375, usbasp::IspClock::KHz187_5, usbasp::IspClock::KHz93_75,
      usbasp::IspClock::KHz32, usbasp::IspClock::KHz16, usbasp::IspClock::KHz8,
      usbasp::IspClock::KHz4, usbasp::IspClock::KHz2, usbasp::IspClock::KHz1, usbasp::IspClock::Hz500};
  } else {
    attempts = {clock};
  }

  QString lastError;
  for (usbasp::IspClock attempt : attempts) {
    if (!m_usbasp.connectTpi(tpiDelayForClock(attempt), lastError)) continue;
    QThread::msleep(100);
    if (m_usbasp.enableTpiProgramming(lastError)) {
      m_clock = attempt;
      m_tpiMode = true;
      m_active = true;
      if (clock == usbasp::IspClock::Auto) {
        warning = QStringLiteral("Auto TPI selected %1.").arg(clockText(attempt));
      } else if (clock == usbasp::IspClock::Pro) {
        warning = QStringLiteral("Pro TPI using the USBasp firmware's fastest/default TPI delay.");
      }
      error.clear();
      return true;
    }
    m_usbasp.disconnectTarget();
  }
  error = lastError.isEmpty() ? QStringLiteral("Target MCU not detected.") : lastError;
  return false;
}

void AvrIspProgrammer::end()
{
    if (m_active) {
        m_usbasp.disconnectTarget();
    }
    m_active = false;
    m_tpiMode = false;
    m_tpiSignatureOffset = 0;
    m_refreshSessionBeforeNextDeviceAccess = false;
}

bool AvrIspProgrammer::active() const
{
    return m_active;
}

bool AvrIspProgrammer::readSignature(QByteArray& signature, QString& error)
{
    if (!m_active) {
        error = QStringLiteral("No active ISP session.");
        return false;
    }

    signature.clear();
    if (m_tpiMode) {
        const quint16 signatureOffset = m_tpiSignatureOffset != 0 ? m_tpiSignatureOffset : 0x3FC0;
        return m_usbasp.tpiReadMemory(signatureOffset, 3, signature, {}, error);
    }
    for (quint8 index = 0; index < 3; ++index) {
        const QByteArray command = makeIspCommand(0x30, 0x00, index, 0x00);
        QByteArray response;
        if (!execute(command, response, error)) {
            return false;
        }
        signature.append(response.at(3));
    }
    return true;
}

bool AvrIspProgrammer::chipErase(const AvrDevice& device, QString& error)
{
    if (!m_active) {
        error = QStringLiteral("No active ISP session.");
        return false;
    }
    if (!validateDevice(device, error)) {
        return false;
    }

    if (device.isTpi()) {
        if (!m_usbasp.tpiChipErase(error)) return false;
        QThread::msleep(static_cast<unsigned long>(std::max(1, device.chipEraseDelayMs)));
        return reenterProgrammingMode(device.resetDelayMs, error);
    }

    QByteArray response;
    const QByteArray command = makeIspCommand(0xAC, 0x80, 0x00, 0x00);
    if (!execute(command, response, error)) {
        return false;
    }

    QThread::msleep(static_cast<unsigned long>(std::max(1, device.chipEraseDelayMs)));
    return reenterProgrammingMode(device.resetDelayMs, error);
}

bool AvrIspProgrammer::readFlash(const AvrDevice& device, FirmwareImage& image, bool fullRead,
                                 const ProgressCallback& progress, QString& error)
{
    m_lastTransferBytes = 0;
    m_lastTransferElapsedMs = 0;
    if (!validateDevice(device, error)) {
        return false;
    }

    image.reset(device.flashSize, 0xFF);
    QByteArray bytes;
    QElapsedTimer transferTimer;
    transferTimer.start();

    if (fullRead) {
        const bool readOk = device.isTpi()
            ? m_usbasp.tpiReadMemory(device.tpiFlashOffset, device.flashSize, bytes, progress, error)
            : m_usbasp.readFlash(0, device.flashSize, m_clock, bytes, progress, error);
        m_lastTransferElapsedMs = transferTimer.elapsed();
        if (!readOk) {
            return false;
        }
        m_lastTransferBytes = bytes.size();
    } else {
        const qsizetype pageSize = std::max<qsizetype>(1, device.flashPageSize);
        qsizetype offset = 0;
        while (offset < device.flashSize) {
            const qsizetype count = std::min(pageSize, static_cast<qsizetype>(device.flashSize) - offset);
            QByteArray page;
            const auto pageProgress = [progress, offset, &device](qsizetype done, qsizetype total) {
                Q_UNUSED(total)
                return !progress || progress(offset + done, device.flashSize);
            };
            const bool readOk = device.isTpi()
                ? m_usbasp.tpiReadMemory(device.tpiFlashOffset + static_cast<quint16>(offset),
                                         count, page, pageProgress, error)
                : m_usbasp.readFlash(static_cast<quint32>(offset), count, m_clock,
                                     page, pageProgress, error);
            if (!readOk) {
                m_lastTransferElapsedMs = transferTimer.elapsed();
                return false;
            }
            if (page.isEmpty()) {
                error = QStringLiteral("Flash read returned no data at address 0x%1.")
                    .arg(offset, 8, 16, QLatin1Char('0'));
                m_lastTransferElapsedMs = transferTimer.elapsed();
                return false;
            }
            m_lastTransferBytes += page.size();
            bytes.append(page);
            offset += page.size();
            if (progress && !progress(offset, device.flashSize)) {
                error = QStringLiteral("Operation cancelled.");
                m_lastTransferElapsedMs = transferTimer.elapsed();
                return false;
            }
            if (allErased(page)) {
                break;
            }
        }
        m_lastTransferElapsedMs = transferTimer.elapsed();
    }

    if (!bytes.isEmpty()) {
        image.setBytes(0, bytes, false);
    }
    image.cleanTrailingErased(bytes.size());
    return true;
}

bool AvrIspProgrammer::readEeprom(const AvrDevice& device, FirmwareImage& image,
                                  const ProgressCallback& progress, QString& error)
{
    m_lastTransferBytes = 0;
    m_lastTransferElapsedMs = 0;
    if (!validateDevice(device, error)) {
        return false;
    }
    if (device.eepromSize == 0) {
        error = QStringLiteral("The selected device has no EEPROM definition.");
        return false;
    }
    QByteArray bytes;
    QElapsedTimer transferTimer;
    transferTimer.start();
    if (!m_usbasp.readEeprom(0, device.eepromSize, m_clock, bytes, progress, error)) {
        m_lastTransferElapsedMs = transferTimer.elapsed();
        return false;
    }
    m_lastTransferElapsedMs = transferTimer.elapsed();
    image.reset(device.eepromSize, 0xFF);
    image.setBytes(0, bytes, true);
    m_lastTransferBytes = bytes.size();
    return true;
}

bool AvrIspProgrammer::writeFlash(const AvrDevice& device, const FirmwareImage& image,
                                  bool eraseFirst, const ProgressCallback& progress,
                                  QString& error)
{
    m_lastTransferBytes = 0;
    m_lastTransferElapsedMs = 0;
    if (image.definedCount() == 0) {
        error.clear();
        return true;
    }
    if (!validateDevice(device, error)) {
        return false;
    }
    if (image.capacity() != device.flashSize) {
        error = QStringLiteral("Flash image capacity (%1) does not match %2 flash size (%3).")
                    .arg(image.capacity()).arg(device.name).arg(device.flashSize);
        return false;
    }
    if (eraseFirst && !chipErase(device, error)) {
        return false;
    }

    const qsizetype pageSize = device.flashPageSize;
    const qsizetype pageCount = (device.flashSize + pageSize - 1) / pageSize;
    std::vector<MemoryRun> runs;

    if (eraseFirst) {
        for (qsizetype page = 0; page < pageCount; ++page) {
            const qsizetype offset = page * pageSize;
            const qsizetype length = std::min<qsizetype>(
                pageSize, static_cast<qsizetype>(device.flashSize) - offset);
            if (!image.anyDefined(offset, length)) {
                continue;
            }
            QByteArray pageData = image.dataForRange(offset, length, 0xFF);
            if (!allErased(pageData)) {
                appendRunPage(runs, static_cast<quint32>(offset), std::move(pageData));
            }
        }
    } else {
        qsizetype page = 0;
        while (page < pageCount) {
            while (page < pageCount) {
                const qsizetype offset = page * pageSize;
                const qsizetype length = std::min<qsizetype>(
                    pageSize, static_cast<qsizetype>(device.flashSize) - offset);
                if (image.anyDefined(offset, length)) {
                    break;
                }
                ++page;
            }
            if (page >= pageCount) {
                break;
            }

            const qsizetype spanFirstPage = page;
            while (page < pageCount) {
                const qsizetype offset = page * pageSize;
                const qsizetype length = std::min<qsizetype>(
                    pageSize, static_cast<qsizetype>(device.flashSize) - offset);
                if (!image.anyDefined(offset, length)) {
                    break;
                }
                ++page;
            }
            const qsizetype spanOffset = spanFirstPage * pageSize;
            const qsizetype spanEnd = std::min<qsizetype>(page * pageSize, device.flashSize);
            const qsizetype spanLength = spanEnd - spanOffset;

            QByteArray current;
            const auto passiveProgress = [progress](qsizetype, qsizetype) {
                return !progress || progress(0, 1);
            };
            const bool currentReadOk = device.isTpi()
                ? m_usbasp.tpiReadMemory(static_cast<quint16>(device.tpiFlashOffset + spanOffset),
                                         spanLength, current, passiveProgress, error)
                : m_usbasp.readFlash(static_cast<quint32>(spanOffset), spanLength, m_clock,
                                     current, passiveProgress, error);
            if (!currentReadOk) {
                return false;
            }

            for (qsizetype spanPage = spanFirstPage; spanPage < page; ++spanPage) {
                const qsizetype offset = spanPage * pageSize;
                const qsizetype length = std::min<qsizetype>(
                    pageSize, static_cast<qsizetype>(device.flashSize) - offset);
                const qsizetype localOffset = offset - spanOffset;
                QByteArray pageData = current.mid(localOffset, length);
                bool changed = false;
                for (qsizetype i = 0; i < length; ++i) {
                    if (!image.isDefined(offset + i)) {
                        continue;
                    }
                    const quint8 existing = static_cast<quint8>(pageData.at(i));
                    const quint8 desired = image.byteAt(offset + i);
                    if ((desired & existing) != desired) {
                        error = QStringLiteral("Flash byte at 0x%1 requires a chip erase (current 0x%2, requested 0x%3).")
                                    .arg(offset + i, 8, 16, QLatin1Char('0'))
                                    .arg(existing, 2, 16, QLatin1Char('0'))
                                    .arg(desired, 2, 16, QLatin1Char('0'));
                        return false;
                    }
                    if (existing != desired) {
                        pageData[i] = static_cast<char>(desired);
                        changed = true;
                    }
                }
                if (changed) {
                    appendRunPage(runs, static_cast<quint32>(offset), std::move(pageData));
                }
            }
        }
    }

    const qsizetype totalBytes = runBytes(runs);
    qsizetype completedBytes = 0;
    QElapsedTimer transferTimer;
    transferTimer.start();
    for (const MemoryRun& run : runs) {
        const auto runProgress = [progress, completedBytes, totalBytes](qsizetype done, qsizetype) {
            return !progress || progress(completedBytes + done, totalBytes);
        };
        const bool writeOk = device.isTpi()
            ? m_usbasp.tpiWriteMemory(static_cast<quint16>(device.tpiFlashOffset + run.address),
                                      run.data, runProgress, error)
            : m_usbasp.writeFlash(run.address, run.data, device.flashPageSize, m_clock,
                                  runProgress, error);
        if (!writeOk) {
            m_lastTransferElapsedMs = transferTimer.elapsed();
            return false;
        }
        completedBytes += run.data.size();
    }

    m_lastTransferBytes = totalBytes;
    m_lastTransferElapsedMs = transferTimer.elapsed();
    m_refreshSessionBeforeNextDeviceAccess = totalBytes > 0;
    if (progress) {
        progress(totalBytes, totalBytes);
    }
    return true;
}

bool AvrIspProgrammer::writeEeprom(const AvrDevice& device, const FirmwareImage& image,
                                   const ProgressCallback& progress, QString& error)
{
    m_lastTransferBytes = 0;
    m_lastTransferElapsedMs = 0;
    if (image.definedCount() == 0) {
        error.clear();
        return true;
    }
    if (!validateDevice(device, error)) {
        return false;
    }
    if (device.eepromSize == 0 || device.eepromPageSize == 0) {
        error = QStringLiteral("The selected device has no writable EEPROM definition.");
        return false;
    }
    if (image.capacity() != device.eepromSize) {
        error = QStringLiteral("EEPROM image capacity (%1) does not match %2 EEPROM size (%3).")
                    .arg(image.capacity()).arg(device.name).arg(device.eepromSize);
        return false;
    }
    const qsizetype pageSize = device.eepromPageSize;
    const qsizetype pageCount = (device.eepromSize + pageSize - 1) / pageSize;
    std::vector<MemoryRun> runs;

    qsizetype page = 0;
    while (page < pageCount) {
        const qsizetype offset = page * pageSize;
        const qsizetype length = std::min<qsizetype>(
            pageSize, static_cast<qsizetype>(device.eepromSize) - offset);
        if (!image.anyDefined(offset, length)) {
            ++page;
            continue;
        }

        if (pageFullyDefined(image, offset, length)) {
            appendRunPage(runs, static_cast<quint32>(offset),
                          image.dataForRange(offset, length, 0xFF));
            ++page;
            continue;
        }

        const qsizetype spanFirstPage = page;
        while (page < pageCount) {
            const qsizetype spanPageOffset = page * pageSize;
            const qsizetype spanPageLength = std::min<qsizetype>(
                pageSize, static_cast<qsizetype>(device.eepromSize) - spanPageOffset);
            if (!image.anyDefined(spanPageOffset, spanPageLength)
                || pageFullyDefined(image, spanPageOffset, spanPageLength)) {
                break;
            }
            ++page;
        }
        const qsizetype spanOffset = spanFirstPage * pageSize;
        const qsizetype spanEnd = std::min<qsizetype>(page * pageSize, device.eepromSize);
        const qsizetype spanLength = spanEnd - spanOffset;
        QByteArray current;
        if (!m_usbasp.readEeprom(static_cast<quint32>(spanOffset), spanLength, m_clock,
                                 current,
                                 [progress](qsizetype, qsizetype) {
                                     return !progress || progress(0, 1);
                                 }, error)) {
            return false;
        }

        for (qsizetype spanPage = spanFirstPage; spanPage < page; ++spanPage) {
            const qsizetype pageOffset = spanPage * pageSize;
            const qsizetype pageLength = std::min<qsizetype>(
                pageSize, static_cast<qsizetype>(device.eepromSize) - pageOffset);
            const qsizetype localOffset = pageOffset - spanOffset;
            QByteArray pageData = current.mid(localOffset, pageLength);
            bool changed = false;
            for (qsizetype i = 0; i < pageLength; ++i) {
                if (!image.isDefined(pageOffset + i)) {
                    continue;
                }
                const quint8 desired = image.byteAt(pageOffset + i);
                if (static_cast<quint8>(pageData.at(i)) != desired) {
                    pageData[i] = static_cast<char>(desired);
                    changed = true;
                }
            }
            if (changed) {
                appendRunPage(runs, static_cast<quint32>(pageOffset), std::move(pageData));
            }
        }
    }

    const qsizetype totalBytes = runBytes(runs);
    qsizetype completedBytes = 0;
    QElapsedTimer transferTimer;
    transferTimer.start();
    for (const MemoryRun& run : runs) {
        if (!m_usbasp.writeEeprom(
              run.address, run.data, device.eepromPageSize, m_clock,
              [progress, completedBytes, totalBytes](qsizetype done, qsizetype) {
                  return !progress || progress(completedBytes + done, totalBytes);
              }, error)) {
            m_lastTransferElapsedMs = transferTimer.elapsed();
            return false;
        }
        completedBytes += run.data.size();
    }

    m_lastTransferBytes = totalBytes;
    m_lastTransferElapsedMs = transferTimer.elapsed();
    m_refreshSessionBeforeNextDeviceAccess = totalBytes > 0;
    if (progress) {
        progress(totalBytes, totalBytes);
    }
    return true;
}

bool AvrIspProgrammer::verifyFlash(const AvrDevice& device, const FirmwareImage& image,
                                   const ProgressCallback& progress, QString& error)
{
    return verifyImage(device, image, true, progress, error);
}

bool AvrIspProgrammer::verifyEeprom(const AvrDevice& device, const FirmwareImage& image,
                                    const ProgressCallback& progress, QString& error)
{
    return verifyImage(device, image, false, progress, error);
}

bool AvrIspProgrammer::blankCheckFlash(const AvrDevice& device,
                                       const ProgressCallback& progress, QString& error)
{
    return blankCheck(device, true, progress, error);
}

bool AvrIspProgrammer::blankCheckEeprom(const AvrDevice& device,
                                        const ProgressCallback& progress, QString& error)
{
    return blankCheck(device, false, progress, error);
}

bool AvrIspProgrammer::readFuses(const AvrDevice& device, QVector<quint8>& values,
                                 QString& error)
{
    if (!validateDevice(device, error)) {
        return false;
    }
    values.clear();
    if (device.isTpi()) {
        QByteArray raw;
        if (!m_usbasp.tpiReadMemory(device.tpiFuseOffset, 1, raw, {}, error)) return false;
        values.append(static_cast<quint8>(raw.at(0)));
        return true;
    }
    for (const QByteArray& command : device.fuseReadCommands) {
        QByteArray response;
        if (!execute(command, response, error)) {
            return false;
        }
        if (device.fuseReadPosition < 0 || device.fuseReadPosition >= response.size()) {
            error = QStringLiteral("Invalid fuse response position in the device database.");
            return false;
        }
        values.append(static_cast<quint8>(response.at(device.fuseReadPosition)));
    }
    return true;
}

bool AvrIspProgrammer::writeFuses(const AvrDevice& device,
                                  const QVector<int>& requestedValues,
                                  QString& error)
{
    QVector<quint8> current;
    if (!readFuses(device, current, error)) {
        return false;
    }

    const int count = std::min({device.fuseCount(), static_cast<int>(requestedValues.size()), static_cast<int>(current.size())});
    for (int i = 0; i < count; ++i) {
        if (requestedValues.at(i) < 0) {
            continue;
        }
        quint8 mask = 0;
        if (i < device.fuseProgramMasks.size()) {
            mask = device.fuseProgramMasks.at(i);
        }
        const quint8 desired = static_cast<quint8>(requestedValues.at(i));
        const quint8 programmed = static_cast<quint8>((current.at(i) & ~mask) | (desired & mask));
        if ((programmed & mask) == (current.at(i) & mask)) {
            continue;
        }

        if (device.isTpi()) {
            if (!m_usbasp.tpiWriteConfigByte(device.tpiFuseOffset, programmed, true, error)) return false;
            continue;
        }

        QByteArray command = device.fuseWriteCommands.at(i);
        if (device.fuseWritePosition < 0 || device.fuseWritePosition >= command.size()) {
            error = QStringLiteral("Invalid fuse command position in the device database.");
            return false;
        }
        command[device.fuseWritePosition] = static_cast<char>(programmed);
        QByteArray response;
        if (!execute(command, response, error)) {
            return false;
        }
        QThread::msleep(static_cast<unsigned long>(std::max(1, device.fuseWriteDelayMs)));
    }

    QVector<quint8> readback;
    if (!readFuses(device, readback, error)) {
        return false;
    }
    for (int i = 0; i < count; ++i) {
        if (requestedValues.at(i) < 0) {
            continue;
        }
        const quint8 mask = i < device.fuseProgramMasks.size()
            ? device.fuseProgramMasks.at(i) : 0;
        if ((readback.at(i) & mask) != (static_cast<quint8>(requestedValues.at(i)) & mask)) {
            error = QStringLiteral("Fuse %1 verification failed: read 0x%2 after requesting 0x%3.")
                        .arg(i)
                        .arg(readback.at(i), 2, 16, QLatin1Char('0'))
                        .arg(requestedValues.at(i), 2, 16, QLatin1Char('0'));
            return false;
        }
    }
    return true;
}

bool AvrIspProgrammer::readLock(const AvrDevice& device, quint8& value, QString& error)
{
    if (!validateDevice(device, error)) {
        return false;
    }
    if (device.isTpi()) {
        QByteArray raw;
        if (!m_usbasp.tpiReadMemory(device.tpiLockOffset, 1, raw, {}, error)) return false;
        value = static_cast<quint8>(raw.at(0));
        return true;
    }
    if (device.lockReadCommand.size() != 4) {
        error = QStringLiteral("Lock-bit reading is not defined for this device.");
        return false;
    }
    QByteArray response;
    if (!execute(device.lockReadCommand, response, error)) {
        return false;
    }
    if (device.lockReadPosition < 0 || device.lockReadPosition >= response.size()) {
        error = QStringLiteral("Invalid lock-bit response position in the device database.");
        return false;
    }
    value = static_cast<quint8>(response.at(device.lockReadPosition));
    return true;
}

bool AvrIspProgrammer::writeLock(const AvrDevice& device, quint8 value, QString& error)
{
    if (!device.hasLockByte()) {
        error = QStringLiteral("Lock-bit writing is not defined for this device.");
        return false;
    }
    if (device.lockProgramMask == 0) {
        error = QStringLiteral("Lock-bit writing is disabled because this device has no safely defined writable lock bits.");
        return false;
    }

    quint8 current = 0xFF;
    if (!readLock(device, current, error)) {
        return false;
    }
    const quint8 mask = device.lockProgramMask;
    const quint8 requestedWritableBits = static_cast<quint8>(value & mask);
    const quint8 impossibleSetBits = static_cast<quint8>(requestedWritableBits
        & static_cast<quint8>(~current));
    if (impossibleSetBits != 0) {
        error = QStringLiteral("Lock bits 0x%1 are already programmed to zero and cannot be restored to one without a chip erase.")
                    .arg(impossibleSetBits, 2, 16, QLatin1Char('0'));
        return false;
    }
    // AVR lock bits are one-way until chip erase. Writing a one leaves a bit
    // unprogrammed, so all undescribed bits are forced to one rather than
    // copying potentially undefined readback values into the write command.
    const quint8 programmed = static_cast<quint8>(static_cast<quint8>(~mask)
                                                   | requestedWritableBits);
    if ((programmed & mask) == (current & mask)) {
        return true;
    }

    if (device.isTpi()) {
        if (!m_usbasp.tpiWriteConfigByte(device.tpiLockOffset, programmed, false, error)) return false;
        quint8 readback = 0;
        if (!readLock(device, readback, error)) return false;
        if ((readback & mask) != (value & mask)) {
            error = QStringLiteral("Lock-bit verification failed: read 0x%1 after requesting 0x%2 (mask 0x%3).")
                        .arg(readback, 2, 16, QLatin1Char('0'))
                        .arg(value, 2, 16, QLatin1Char('0'))
                        .arg(mask, 2, 16, QLatin1Char('0'));
            return false;
        }
        return true;
    }

    QByteArray command = device.lockWriteCommand;
    if (device.lockWritePosition < 0 || device.lockWritePosition >= command.size()) {
        error = QStringLiteral("Invalid lock-bit command position in the device database.");
        return false;
    }
    command[device.lockWritePosition] = static_cast<char>(programmed);
    QByteArray response;
    if (!execute(command, response, error)) {
        return false;
    }
    QThread::msleep(static_cast<unsigned long>(std::max(1, device.lockWriteDelayMs)));

    quint8 readback = 0;
    if (!readLock(device, readback, error)) {
        return false;
    }
    if ((readback & mask) != (value & mask)) {
        error = QStringLiteral("Lock-bit verification failed: read 0x%1 after requesting 0x%2 (mask 0x%3).")
                    .arg(readback, 2, 16, QLatin1Char('0'))
                    .arg(value, 2, 16, QLatin1Char('0'))
                    .arg(mask, 2, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

quint32 AvrIspProgrammer::capabilities() const
{
    return m_capabilities;
}

qsizetype AvrIspProgrammer::lastTransferBytes() const
{
    return m_lastTransferBytes;
}

qint64 AvrIspProgrammer::lastTransferElapsedMs() const
{
    return m_lastTransferElapsedMs;
}

bool AvrIspProgrammer::validateDevice(const AvrDevice& device, QString& error)
{
    if (device.isTpi() != m_tpiMode) {
        error = QStringLiteral("The active programmer session does not match the selected MCU programming interface.");
        return false;
    }
    if (m_refreshSessionBeforeNextDeviceAccess) {
        if (!reenterProgrammingMode(device.resetDelayMs, error)) {
            return false;
        }
        m_refreshSessionBeforeNextDeviceAccess = false;
    }

    if (m_ignoreSignatureMatching) {
        error.clear();
        return true;
    }

    QByteArray signature;
    if (!readSignature(signature, error)) {
        return false;
    }
    if (signature != device.signature) {
        error = QStringLiteral("Target signature mismatch. Selected %1 expects %2, but the target returned %3.")
                    .arg(device.name, device.signatureText(),
                         QString::fromLatin1(signature.toHex(' ').toUpper()));
        return false;
    }
    return true;
}

bool AvrIspProgrammer::reenterProgrammingMode(int resetDelayMs, QString& error)
{
    m_usbasp.disconnectTarget();
    QThread::msleep(static_cast<unsigned long>(std::max(20, resetDelayMs)));

    if (!m_tpiMode) {
        QString clockError;
        if (!m_usbasp.setIspClock(m_clock, clockError)
            && m_clock != usbasp::IspClock::Pro) {
            error = clockError;
            m_active = false;
            return false;
        }
    }

    const bool connected = m_tpiMode
        ? m_usbasp.connectTpi(tpiDelayForClock(m_clock), error)
        : m_usbasp.connectTarget(error);
    if (!connected) {
        m_active = false;
        return false;
    }
    QThread::msleep(static_cast<unsigned long>(std::max(20, resetDelayMs)));
    const bool enabled = m_tpiMode ? m_usbasp.enableTpiProgramming(error)
                                   : m_usbasp.enableProgramming(error);
    if (!enabled) {
        m_active = false;
        return false;
    }
    return true;
}

bool AvrIspProgrammer::execute(const QByteArray& command, QByteArray& response,
                               QString& error)
{
    if (command.size() != 4) {
        error = QStringLiteral("AVR ISP commands must contain exactly four bytes.");
        return false;
    }
    std::array<quint8, 4> input{};
    std::array<quint8, 4> output{};
    for (int i = 0; i < 4; ++i) {
        input[static_cast<std::size_t>(i)] = static_cast<quint8>(command.at(i));
    }
    if (!m_usbasp.transmit(input, output, error)) {
        return false;
    }
    response.resize(4);
    for (int i = 0; i < 4; ++i) {
        response[i] = static_cast<char>(output[static_cast<std::size_t>(i)]);
    }
    return true;
}

bool AvrIspProgrammer::verifyImage(const AvrDevice& device, const FirmwareImage& image,
                                   bool flash, const ProgressCallback& progress,
                                   QString& error)
{
    if (!validateDevice(device, error)) {
        return false;
    }
    const qsizetype expectedCapacity = flash ? device.flashSize : device.eepromSize;
    if (image.capacity() != expectedCapacity) {
        error = QStringLiteral("Image capacity does not match the selected device memory.");
        return false;
    }
    if (image.definedCount() == 0) {
        error = QStringLiteral("Image contains no defined bytes to verify.");
        return false;
    }

    const qsizetype highestDefined = image.highestDefinedAddress();
    const qsizetype verifyLength = highestDefined + 1;
    QByteArray target;
    bool readOk = false;
    if (flash && device.isTpi()) {
        readOk = m_usbasp.tpiReadMemory(device.tpiFlashOffset, verifyLength, target, progress, error);
    } else if (flash) {
        readOk = m_usbasp.readFlash(0, verifyLength, m_clock, target, progress, error);
    } else {
        readOk = m_usbasp.readEeprom(0, verifyLength, m_clock, target, progress, error);
    }
    if (!readOk) {
        return false;
    }

    qsizetype mismatch = -1;
    quint8 expected = 0;
    quint8 actual = 0;
    if (!image.compareDefined(target, mismatch, expected, actual)) {
        error = QStringLiteral("Verification mismatch at 0x%1: expected 0x%2, read 0x%3.")
                    .arg(mismatch, 8, 16, QLatin1Char('0'))
                    .arg(expected, 2, 16, QLatin1Char('0'))
                    .arg(actual, 2, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool AvrIspProgrammer::blankCheck(const AvrDevice& device, bool flash,
                                  const ProgressCallback& progress, QString& error)
{
    if (!validateDevice(device, error)) {
        return false;
    }
    const qsizetype size = flash ? device.flashSize : device.eepromSize;
    if (size <= 0) {
        error = QStringLiteral("The selected memory is not defined for this device.");
        return false;
    }

    QByteArray target;
    bool readOk = false;
    if (flash && device.isTpi()) {
        readOk = m_usbasp.tpiReadMemory(device.tpiFlashOffset, size, target, progress, error);
    } else if (flash) {
        readOk = m_usbasp.readFlash(0, size, m_clock, target, progress, error);
    } else {
        readOk = m_usbasp.readEeprom(0, size, m_clock, target, progress, error);
    }
    if (!readOk) {
        return false;
    }
    for (qsizetype i = 0; i < target.size(); ++i) {
        if (static_cast<quint8>(target.at(i)) != 0xFFu) {
            error = QStringLiteral("Memory is not blank at 0x%1; read 0x%2.")
                        .arg(i, 8, 16, QLatin1Char('0'))
                        .arg(static_cast<quint8>(target.at(i)), 2, 16, QLatin1Char('0'));
            return false;
        }
    }
    return true;
}

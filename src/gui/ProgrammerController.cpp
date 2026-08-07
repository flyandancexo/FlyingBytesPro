// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/ProgrammerController.h"

#include "avr/AvrIspProgrammer.h"
#include "usb/UsbAspDevice.h"

#include <QElapsedTimer>
#include <QSettings>
#include <QThread>

#include <algorithm>

namespace {

bool ignoreMcuSignatureMatchingSetting() {
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.sync();
  return settings.value(QStringLiteral("ignoreMcuSignatureMatching"), false).toBool();
}

QString usbInfoText(const UsbAspDevice::DeviceInfo& info)
{
    return QStringLiteral("USBasp opened: VID:PID %1:%2, bus %3, address %4, product '%5', serial '%6'.")
        .arg(info.vid, 4, 16, QLatin1Char('0'))
        .arg(info.pid, 4, 16, QLatin1Char('0'))
        .arg(static_cast<int>(info.bus))
        .arg(static_cast<int>(info.address))
        .arg(info.product.isEmpty() ? QStringLiteral("(not reported)") : info.product)
        .arg(info.serialNumber.isEmpty() ? QStringLiteral("(not reported)") : info.serialNumber)
        .toUpper();
}

QString signatureText(QByteArrayView signature)
{
    QString result;
    for (qsizetype i = 0; i < signature.size(); ++i) {
        if (!result.isEmpty()) {
            result += QLatin1Char(' ');
        }
        result += QStringLiteral("%1")
            .arg(static_cast<quint8>(signature.at(i)), 2, 16, QLatin1Char('0'))
            .toUpper();
    }
    return result;
}

QString transferRateText(qsizetype bytes, qint64 elapsedMs) {
  const qint64 safeMs = std::max<qint64>(1, elapsedMs);
  const double bytesPerSecond = static_cast<double>(bytes) * 1000.0
    / static_cast<double>(safeMs);
  if (bytesPerSecond < 1024.0) {
    return QStringLiteral("%1 B/s").arg(bytesPerSecond, 0, 'f', 0);
  }
  return QStringLiteral("%1 KB/s").arg(bytesPerSecond / 1024.0, 0, 'f', 1);
}

QString elapsedTimeText(qint64 elapsedMs) {
  return QStringLiteral("%1s")
    .arg(static_cast<double>(std::max<qint64>(0, elapsedMs)) / 1000.0, 0, 'f', 2);
}

} // namespace

ProgrammerController::ProgrammerController(bool demoMode, QObject* parent)
    : QObject(parent), m_demoMode(demoMode)
{
}

void ProgrammerController::requestCancel() noexcept
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool ProgrammerController::demoMode() const noexcept
{
    return m_demoMode;
}

void ProgrammerController::probe(int clockId, bool quiet)
{
    if (!quiet) {
        beginOperation(QStringLiteral("Probe USBasp"));
    }
    if (m_demoMode) {
        if (!quiet) {
            QThread::msleep(100);
            finishProgress();
            Q_EMIT logMessage(QStringLiteral("Demo mode: simulated USBasp opened successfully."));
        }
        Q_EMIT probeFinished(true, QStringLiteral("Demo USBasp is available."), 0, quiet);
        return;
    }

    UsbAspDevice usb;
    if (!quiet) {
        attachTrace(usb);
    }
    QString error;
    if (!usb.open(error)) {
        Q_EMIT probeFinished(false, error, 0, quiet);
        return;
    }
    if (!quiet) {
        Q_EMIT logMessage(usbInfoText(usb.info()));
    }

    quint32 capabilities = 0;
    QString capabilityError;
    if (!usb.queryCapabilities(capabilities, capabilityError)) {
        if (!quiet) {
            Q_EMIT logMessage(QStringLiteral("Capability query not supported: %1").arg(capabilityError));
        }
    } else if (!quiet) {
        Q_EMIT logMessage(QStringLiteral("USBasp capabilities: 0x%1")
                              .arg(capabilities, 8, 16, QLatin1Char('0')).toUpper());
    }

    if (!quiet) {
        const usbasp::IspClock probeClock = clockFromId(clockId);
        if (probeClock != usbasp::IspClock::Auto) {
            QString clockError;
            if (!usb.setIspClock(probeClock, clockError)) {
                Q_EMIT logMessage(QStringLiteral("ISP clock selection was not accepted: %1").arg(clockError));
            }
        }
        finishProgress();
    }
    usb.close();
    Q_EMIT probeFinished(true, QStringLiteral("Programmer Online :)"), capabilities, quiet);
}

void ProgrammerController::detectTarget(int clockId)
{
    beginOperation(QStringLiteral("Detect target"));
    if (m_demoMode) {
        const QByteArray signature = QByteArray::fromHex("1E950F");
        QThread::msleep(150);
        finishProgress();
        Q_EMIT signatureFinished(true,
                                 QStringLiteral("Demo target signature: %1")
                                     .arg(signatureText(signature)),
                                 signature);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT signatureFinished(false, error, {});
        return;
    }
    Q_EMIT logMessage(usbInfoText(usb.info()));

    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    bool started = programmer.begin(clockFromId(clockId), warning, error);
    if (!started) {
        QString tpiWarning;
        QString tpiError;
        started = programmer.beginTpi(clockFromId(clockId), tpiWarning, tpiError);
        if (started) {
            warning = tpiWarning;
        } else {
            Q_EMIT signatureFinished(false, error.isEmpty() ? tpiError : error, {});
            return;
        }
    }
    if (!warning.isEmpty()) {
        Q_EMIT logMessage(warning);
    }

    QByteArray signature;
    const bool ok = programmer.readSignature(signature, error);
    programmer.end();
    finishProgress();
    if (!ok) {
        Q_EMIT signatureFinished(false, error, {});
        return;
    }
    Q_EMIT signatureFinished(true,
                             QStringLiteral("Target signature: %1")
                                 .arg(signatureText(signature)),
                             signature);
}

void ProgrammerController::readFlash(AvrDevice device, int clockId, bool fullRead) {
  beginOperation(QStringLiteral("Read Flash"));
  if (m_demoMode) {
    ensureDemoDevice(device);
    QElapsedTimer timer;
    timer.start();
    QThread::msleep(40);
    const qint64 elapsedMs = timer.elapsed();
    finishProgress();
    FirmwareImage image(device.flashSize, 0xFF);
    qsizetype physicalBytes = device.flashSize;
    qsizetype limit = device.flashSize;
    if (!fullRead) {
      const qsizetype pageSize = std::max<qsizetype>(1, device.flashPageSize);
      limit = 0;
      for (qsizetype offset = 0; offset < device.flashSize; offset += pageSize) {
        const qsizetype count = std::min(pageSize, static_cast<qsizetype>(device.flashSize) - offset);
        bool blank = true;
        for (qsizetype i = 0; i < count; ++i) {
          if (m_demoFlash.byteAt(offset + i) != 0xFF) { blank = false; break; }
        }
        limit = offset + count;
        if (blank) { physicalBytes = limit; break; }
      }
    }
    if (limit > 0) {
      image.setBytes(0, QByteArrayView(m_demoFlash.bytes()).first(limit), false);
    }
    const qsizetype used = image.cleanTrailingErased(limit);
    Q_EMIT imageFinished(QStringLiteral("read-flash"), true,
      QStringLiteral("%1 bytes read from Flash at %2 in %3; %4 bytes used")
        .arg(physicalBytes)
        .arg(transferRateText(physicalBytes, elapsedMs), elapsedTimeText(elapsedMs))
        .arg(used),
      image);
    return;
  }

  UsbAspDevice usb;
  attachTrace(usb);
  QString error;
  FirmwareImage image;
  if (!usb.open(error)) {
    Q_EMIT imageFinished(QStringLiteral("read-flash"), false, error, image);
    return;
  }
  AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
  QString warning;
  if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
    Q_EMIT imageFinished(QStringLiteral("read-flash"), false, error, image);
    return;
  }
  if (!warning.isEmpty()) Q_EMIT logMessage(warning);
  const bool ok = programmer.readFlash(device, image, fullRead,
    [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  const qint64 elapsedMs = programmer.lastTransferElapsedMs();
  programmer.end();
  finishProgress();
  Q_EMIT imageFinished(QStringLiteral("read-flash"), ok,
    ok ? QStringLiteral("%1 bytes read from Flash at %2 in %3; %4 bytes used")
           .arg(programmer.lastTransferBytes())
           .arg(transferRateText(programmer.lastTransferBytes(), elapsedMs), elapsedTimeText(elapsedMs))
           .arg(image.definedCount())
       : error,
    image);
}

void ProgrammerController::readEeprom(AvrDevice device, int clockId) {
  beginOperation(QStringLiteral("Read EEPROM"));
  if (m_demoMode) {
    ensureDemoDevice(device);
    QElapsedTimer timer;
    timer.start();
    QThread::msleep(40);
    const qint64 elapsedMs = timer.elapsed();
    finishProgress();
    Q_EMIT imageFinished(QStringLiteral("read-eeprom"), true,
      QStringLiteral("%1 bytes read from EEPROM at %2 in %3")
        .arg(device.eepromSize)
        .arg(transferRateText(device.eepromSize, elapsedMs), elapsedTimeText(elapsedMs)),
      m_demoEeprom);
    return;
  }

  UsbAspDevice usb;
  attachTrace(usb);
  QString error;
  FirmwareImage image;
  if (!usb.open(error)) {
    Q_EMIT imageFinished(QStringLiteral("read-eeprom"), false, error, image);
    return;
  }
  AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
  QString warning;
  if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
    Q_EMIT imageFinished(QStringLiteral("read-eeprom"), false, error, image);
    return;
  }
  if (!warning.isEmpty()) Q_EMIT logMessage(warning);
  const bool ok = programmer.readEeprom(device, image,
    [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  const qint64 elapsedMs = programmer.lastTransferElapsedMs();
  programmer.end();
  finishProgress();
  Q_EMIT imageFinished(QStringLiteral("read-eeprom"), ok,
    ok ? QStringLiteral("%1 bytes read from EEPROM at %2 in %3")
           .arg(programmer.lastTransferBytes())
           .arg(transferRateText(programmer.lastTransferBytes(), elapsedMs), elapsedTimeText(elapsedMs))
       : error,
    image);
}

void ProgrammerController::writeFlash(AvrDevice device, FirmwareImage image,
                                      bool eraseFirst, bool verifyAfter, int clockId) {
  beginOperation(QStringLiteral("Write Flash"));
  if (m_demoMode) {
    ensureDemoDevice(device);
    if (eraseFirst) {
      m_demoFlash.clear();
      m_demoFlash.markAllDefined();
    }
    QElapsedTimer timer;
    timer.start();
    for (qsizetype i = 0; i < image.capacity(); ++i) {
      if (image.isDefined(i)) {
        const quint8 oldValue = m_demoFlash.byteAt(i);
        const quint8 newValue = image.byteAt(i);
        if (!eraseFirst && (newValue & oldValue) != newValue) {
          Q_EMIT operationFinished(QStringLiteral("write-flash"), false,
            QStringLiteral("Demo flash byte at 0x%1 requires erase.")
              .arg(i, 8, 16, QLatin1Char('0')));
          return;
        }
        m_demoFlash.setByte(i, newValue, true);
      }
      if ((i % 256) == 0 && !progress(i, image.capacity())) {
        Q_EMIT operationFinished(QStringLiteral("write-flash"), false,
          QStringLiteral("Operation cancelled."));
        return;
      }
    }
    const qint64 writeElapsedMs = timer.elapsed();
    QString error;
    const bool verified = !verifyAfter || demoVerify(image, m_demoFlash, error);
    const qint64 totalElapsedMs = timer.elapsed();
    finishProgress();
    const QString verifySuffix = verifyAfter
      ? QStringLiteral(" [Verified; total %1]").arg(elapsedTimeText(totalElapsedMs))
      : QString();
    Q_EMIT operationFinished(QStringLiteral("write-flash"), verified,
      verified
        ? QStringLiteral("%1 bytes written to Flash at %2 in %3%4")
            .arg(image.definedCount())
            .arg(transferRateText(image.definedCount(), writeElapsedMs),
                 elapsedTimeText(writeElapsedMs), verifySuffix)
        : error);
    return;
  }

  UsbAspDevice usb;
  attachTrace(usb);
  QString error;
  if (!usb.open(error)) {
    Q_EMIT operationFinished(QStringLiteral("write-flash"), false, error);
    return;
  }
  AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
  QString warning;
  if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
    Q_EMIT operationFinished(QStringLiteral("write-flash"), false, error);
    return;
  }
  if (!warning.isEmpty()) Q_EMIT logMessage(warning);
  QElapsedTimer operationTimer;
  operationTimer.start();
  bool ok = programmer.writeFlash(device, image, eraseFirst,
    [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  const qint64 writeElapsedMs = programmer.lastTransferElapsedMs();
  const qsizetype transferredBytes = programmer.lastTransferBytes();
  if (ok && verifyAfter) {
    Q_EMIT logMessage(QStringLiteral("Verifying written Flash bytes."));
    ok = programmer.verifyFlash(device, image,
      [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  }
  const qint64 totalElapsedMs = operationTimer.elapsed();
  programmer.end();
  finishProgress();
  const QString verifySuffix = verifyAfter
    ? QStringLiteral(" [Verified; total %1]").arg(elapsedTimeText(totalElapsedMs))
    : QString();
  Q_EMIT operationFinished(QStringLiteral("write-flash"), ok,
    ok
      ? QStringLiteral("%1 bytes written to Flash at %2 in %3%4")
          .arg(transferredBytes)
          .arg(transferRateText(transferredBytes, writeElapsedMs),
               elapsedTimeText(writeElapsedMs), verifySuffix)
      : error);
}

void ProgrammerController::writeEeprom(AvrDevice device, FirmwareImage image,
                                       bool verifyAfter, int clockId) {
  beginOperation(QStringLiteral("Write EEPROM"));
  if (m_demoMode) {
    ensureDemoDevice(device);
    QElapsedTimer timer;
    timer.start();
    for (qsizetype i = 0; i < image.capacity(); ++i) {
      if (image.isDefined(i)) {
        m_demoEeprom.setByte(i, image.byteAt(i), true);
      }
      if ((i % 128) == 0 && !progress(i, image.capacity())) {
        Q_EMIT operationFinished(QStringLiteral("write-eeprom"), false,
          QStringLiteral("Operation cancelled."));
        return;
      }
    }
    const qint64 writeElapsedMs = timer.elapsed();
    QString error;
    const bool verified = !verifyAfter || demoVerify(image, m_demoEeprom, error);
    const qint64 totalElapsedMs = timer.elapsed();
    finishProgress();
    const QString verifySuffix = verifyAfter
      ? QStringLiteral(" [Verified; total %1]").arg(elapsedTimeText(totalElapsedMs))
      : QString();
    Q_EMIT operationFinished(QStringLiteral("write-eeprom"), verified,
      verified
        ? QStringLiteral("%1 bytes written to EEPROM at %2 in %3%4")
            .arg(image.definedCount())
            .arg(transferRateText(image.definedCount(), writeElapsedMs),
                 elapsedTimeText(writeElapsedMs), verifySuffix)
        : error);
    return;
  }

  UsbAspDevice usb;
  attachTrace(usb);
  QString error;
  if (!usb.open(error)) {
    Q_EMIT operationFinished(QStringLiteral("write-eeprom"), false, error);
    return;
  }
  AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
  QString warning;
  if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
    Q_EMIT operationFinished(QStringLiteral("write-eeprom"), false, error);
    return;
  }
  if (!warning.isEmpty()) Q_EMIT logMessage(warning);
  QElapsedTimer operationTimer;
  operationTimer.start();
  bool ok = programmer.writeEeprom(device, image,
    [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  const qint64 writeElapsedMs = programmer.lastTransferElapsedMs();
  const qsizetype transferredBytes = programmer.lastTransferBytes();
  if (ok && verifyAfter) {
    Q_EMIT logMessage(QStringLiteral("Verifying written EEPROM bytes."));
    ok = programmer.verifyEeprom(device, image,
      [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
  }
  const qint64 totalElapsedMs = operationTimer.elapsed();
  programmer.end();
  finishProgress();
  const QString verifySuffix = verifyAfter
    ? QStringLiteral(" [Verified; total %1]").arg(elapsedTimeText(totalElapsedMs))
    : QString();
  Q_EMIT operationFinished(QStringLiteral("write-eeprom"), ok,
    ok
      ? QStringLiteral("%1 bytes written to EEPROM at %2 in %3%4")
          .arg(transferredBytes)
          .arg(transferRateText(transferredBytes, writeElapsedMs),
               elapsedTimeText(writeElapsedMs), verifySuffix)
      : error);
}

void ProgrammerController::verifyFlash(AvrDevice device, FirmwareImage image, int clockId)
{
    beginOperation(QStringLiteral("Verify flash"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        QString error;
        const bool ok = demoVerify(image, m_demoFlash, error);
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("verify-flash"), ok,
                                 ok ? QStringLiteral("Demo flash verification passed.") : error);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("verify-flash"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("verify-flash"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.verifyFlash(device, image,
        [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("verify-flash"), ok,
                             ok ? QStringLiteral("Flash verification passed.") : error);
}

void ProgrammerController::verifyEeprom(AvrDevice device, FirmwareImage image, int clockId)
{
    beginOperation(QStringLiteral("Verify EEPROM"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        QString error;
        const bool ok = demoVerify(image, m_demoEeprom, error);
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("verify-eeprom"), ok,
                                 ok ? QStringLiteral("Demo EEPROM verification passed.") : error);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("verify-eeprom"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("verify-eeprom"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.verifyEeprom(device, image,
        [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("verify-eeprom"), ok,
                             ok ? QStringLiteral("EEPROM verification passed.") : error);
}

void ProgrammerController::eraseChip(AvrDevice device, int clockId)
{
    beginOperation(QStringLiteral("Chip erase"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        m_demoFlash.clear();
        m_demoFlash.markAllDefined();
        m_demoEeprom.clear();
        m_demoEeprom.markAllDefined();
        m_demoLock = 0xFF;
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("erase"), true,
                                 QStringLiteral("Demo chip erase completed."));
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("erase"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("erase"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.chipErase(device, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("erase"), ok,
                             ok ? QStringLiteral("Chip erase completed.") : error);
}

void ProgrammerController::blankCheckFlash(AvrDevice device, int clockId)
{
    beginOperation(QStringLiteral("Blank-check flash"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        QString error;
        const bool ok = demoBlank(m_demoFlash, error);
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("blank-flash"), ok,
                                 ok ? QStringLiteral("Demo flash is blank.") : error);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("blank-flash"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("blank-flash"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.blankCheckFlash(device,
        [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("blank-flash"), ok,
                             ok ? QStringLiteral("Flash is blank.") : error);
}

void ProgrammerController::blankCheckEeprom(AvrDevice device, int clockId)
{
    beginOperation(QStringLiteral("Blank-check EEPROM"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        QString error;
        const bool ok = demoBlank(m_demoEeprom, error);
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("blank-eeprom"), ok,
                                 ok ? QStringLiteral("Demo EEPROM is blank.") : error);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("blank-eeprom"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("blank-eeprom"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.blankCheckEeprom(device,
        [this](qsizetype done, qsizetype total) { return progress(done, total); }, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("blank-eeprom"), ok,
                             ok ? QStringLiteral("EEPROM is blank.") : error);
}

void ProgrammerController::readFuses(AvrDevice device, int clockId)
{
    beginOperation(QStringLiteral("Read fuses"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        finishProgress();
        Q_EMIT fusesFinished(true, QStringLiteral("Demo fuses read."),
                             m_demoFuses, m_demoLock);
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT fusesFinished(false, error, {}, -1);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT fusesFinished(false, error, {}, -1);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);

    QVector<quint8> raw;
    bool ok = programmer.readFuses(device, raw, error);
    int lock = -1;
    if (ok && device.hasLockByte()) {
        quint8 lockByte = 0xFF;
        ok = programmer.readLock(device, lockByte, error);
        lock = lockByte;
    }
    programmer.end();

    QVector<int> values;
    values.reserve(raw.size());
    for (quint8 value : raw) values.append(value);
    finishProgress();
    Q_EMIT fusesFinished(ok, ok ? QStringLiteral("Fuses read successfully.") : error,
                         values, lock);
}

void ProgrammerController::writeFuses(AvrDevice device, QVector<int> values, int clockId)
{
    beginOperation(QStringLiteral("Write fuses"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        const int count = std::min(device.fuseCount(), static_cast<int>(values.size()));
        for (int i = 0; i < count; ++i) {
            if (values.at(i) < 0) continue;
            const int mask = i < device.fuseProgramMasks.size()
                ? device.fuseProgramMasks.at(i) : 0;
            m_demoFuses[i] = (m_demoFuses.at(i) & ~mask) | (values.at(i) & mask);
        }
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("write-fuses"), true,
                                 QStringLiteral("Demo fuses written and read back."));
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("write-fuses"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("write-fuses"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.writeFuses(device, values, error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("write-fuses"), ok,
                             ok ? QStringLiteral("Fuses written and verified.") : error);
}

void ProgrammerController::writeLock(AvrDevice device, int value, int clockId)
{
    beginOperation(QStringLiteral("Write lock bits"));
    if (m_demoMode) {
        ensureDemoDevice(device);
        const int mask = device.lockProgramMask;
        m_demoLock = (m_demoLock & ~mask) | (value & mask);
        finishProgress();
        Q_EMIT operationFinished(QStringLiteral("write-lock"), true,
                                 QStringLiteral("Demo lock byte written and read back."));
        return;
    }

    UsbAspDevice usb;
    attachTrace(usb);
    QString error;
    if (!usb.open(error)) {
        Q_EMIT operationFinished(QStringLiteral("write-lock"), false, error);
        return;
    }
    AvrIspProgrammer programmer(usb, ignoreMcuSignatureMatchingSetting());
    QString warning;
    if (!programmer.begin(device, clockFromId(clockId), warning, error)) {
        Q_EMIT operationFinished(QStringLiteral("write-lock"), false, error);
        return;
    }
    if (!warning.isEmpty()) Q_EMIT logMessage(warning);
    const bool ok = programmer.writeLock(device, static_cast<quint8>(value), error);
    programmer.end();
    finishProgress();
    Q_EMIT operationFinished(QStringLiteral("write-lock"), ok,
                             ok ? QStringLiteral("Lock byte written and verified.") : error);
}


void ProgrammerController::attachTrace(UsbAspDevice& usb)
{
    usb.setTraceCallback([this](const QString& message) {
        Q_EMIT logMessage(message);
    });
}

usbasp::IspClock ProgrammerController::clockFromId(int id) const
{
    if (id < static_cast<int>(usbasp::IspClock::Pro)
        || id > static_cast<int>(usbasp::IspClock::Auto)) {
        return usbasp::IspClock::Pro;
    }
    return static_cast<usbasp::IspClock>(id);
}

bool ProgrammerController::progress(qsizetype completed, qsizetype total)
{
    if (m_cancelRequested.load(std::memory_order_relaxed)) {
        return false;
    }
    const int percent = total > 0
        ? static_cast<int>((100.0 * static_cast<double>(completed))
                           / static_cast<double>(total))
        : 0;
    const int clamped = std::clamp(percent, 0, 100);
    const bool finalUpdate = clamped >= 100;
    const bool enoughTimeElapsed = !m_progressEmitTimer.isValid()
        || m_progressEmitTimer.elapsed() >= 40;
    if (clamped != m_lastProgressPercent && (finalUpdate || enoughTimeElapsed)) {
        m_lastProgressPercent = clamped;
        m_progressEmitTimer.restart();
        Q_EMIT progressChanged(clamped);
    }
    return true;
}

void ProgrammerController::beginOperation(const QString& name)
{
    m_cancelRequested.store(false, std::memory_order_relaxed);
    m_lastProgressPercent = 0;
    m_progressEmitTimer.restart();
    Q_EMIT progressChanged(0);
    Q_EMIT logMessage(QStringLiteral("--- %1 ---").arg(name));
}

void ProgrammerController::finishProgress()
{
    Q_EMIT progressChanged(100);
}

void ProgrammerController::ensureDemoDevice(const AvrDevice& device)
{
    if (m_demoDeviceId == device.id
        && m_demoFlash.capacity() == device.flashSize
        && m_demoEeprom.capacity() == device.eepromSize) {
        return;
    }
    m_demoDeviceId = device.id;
    m_demoFlash.reset(device.flashSize, 0xFF);
    m_demoFlash.markAllDefined();
    m_demoEeprom.reset(device.eepromSize, 0xFF);
    m_demoEeprom.markAllDefined();
    m_demoFuses = QVector<int>(device.fuseCount(), 0xFF);
    m_demoLock = 0xFF;
}

bool ProgrammerController::demoVerify(const FirmwareImage& requested,
                                      const FirmwareImage& memory,
                                      QString& error) const
{
    qsizetype mismatch = -1;
    quint8 expected = 0;
    quint8 actual = 0;
    if (!requested.compareDefined(memory.bytes(), mismatch, expected, actual)) {
        error = QStringLiteral("Demo verification mismatch at 0x%1: expected 0x%2, read 0x%3.")
            .arg(mismatch, 8, 16, QLatin1Char('0'))
            .arg(expected, 2, 16, QLatin1Char('0'))
            .arg(actual, 2, 16, QLatin1Char('0'));
        return false;
    }
    return true;
}

bool ProgrammerController::demoBlank(const FirmwareImage& memory, QString& error) const
{
    for (qsizetype i = 0; i < memory.capacity(); ++i) {
        if (memory.byteAt(i) != 0xFF) {
            error = QStringLiteral("Demo memory is not blank at 0x%1; read 0x%2.")
                .arg(i, 8, 16, QLatin1Char('0'))
                .arg(memory.byteAt(i), 2, 16, QLatin1Char('0'));
            return false;
        }
    }
    return true;
}

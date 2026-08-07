// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrIspProgrammer.h"
#include "usb/UsbAspDevice.h"
#include "usb/UsbAspProtocol.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

namespace {

QString signatureText(QByteArrayView signature)
{
    return QString::fromLatin1(QByteArray(signature.data(), signature.size())
                                   .toHex(' ').toUpper());
}

usbasp::IspClock parseClock(const QString& text, bool& ok)
{
    const int value = text.toInt(&ok);
    if (!ok || value < 0 || value > 14) {
        ok = false;
        return usbasp::IspClock::Pro;
    }
    return static_cast<usbasp::IspClock>(value);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("usbasp_probe"));
    QCoreApplication::setApplicationVersion(QStringLiteral("V3.2.20"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Read-only USBasp transport test. By default it opens the programmer only."));
    parser.addHelpOption();
    parser.addVersionOption();

    const QCommandLineOption clockOption(
        QStringList{QStringLiteral("c"), QStringLiteral("clock")},
        QStringLiteral("USBasp ISP clock ID: 0 Pro, 1-13 fixed clocks, 14 Auto scan."),
        QStringLiteral("id"), QStringLiteral("0"));
    const QCommandLineOption targetOption(
        QStringList{QStringLiteral("t"), QStringLiteral("target")},
        QStringLiteral("Also enter target programming mode (SPI ISP first, then TPI) and read the three-byte signature. No write or erase is performed."));
    parser.addOption(clockOption);
    parser.addOption(targetOption);
    parser.process(application);

    QTextStream out(stdout);
    QTextStream err(stderr);

    bool clockOk = false;
    const usbasp::IspClock clock = parseClock(parser.value(clockOption), clockOk);
    if (!clockOk) {
        err << "Invalid --clock value. Use an integer from 0 through 14.\n";
        return 2;
    }

    UsbAspDevice usbasp;
    QString error;
    if (!usbasp.open(error)) {
        err << "FAIL: " << error << '\n';
        return 3;
    }

    const auto& info = usbasp.info();
    out << "PASS: USBasp opened\n"
        << "  VID:PID: "
        << QStringLiteral("%1:%2")
               .arg(info.vid, 4, 16, QLatin1Char('0'))
               .arg(info.pid, 4, 16, QLatin1Char('0')).toUpper() << '\n'
        << "  Bus/address: " << static_cast<int>(info.bus) << '/' << static_cast<int>(info.address) << '\n'
        << "  Manufacturer: " << (info.manufacturer.isEmpty() ? "(not reported)" : info.manufacturer) << '\n'
        << "  Product: " << (info.product.isEmpty() ? "(not reported)" : info.product) << '\n'
        << "  Serial: " << (info.serialNumber.isEmpty() ? "(not reported)" : info.serialNumber) << '\n';

    quint32 capabilities = 0;
    QString capabilityError;
    if (usbasp.queryCapabilities(capabilities, capabilityError)) {
        out << "  Capabilities: 0x"
            << QStringLiteral("%1").arg(capabilities, 8, 16, QLatin1Char('0')).toUpper()
            << '\n';
    } else {
        out << "  Capabilities: not supported (common on older firmware): "
            << capabilityError << '\n';
    }

    if (clock != usbasp::IspClock::Auto) {
        QString clockError;
        if (usbasp.setIspClock(clock, clockError)) {
            out << "  ISP clock request accepted: ID " << static_cast<int>(clock) << '\n';
        } else {
            out << "  ISP clock request not accepted: " << clockError
                << "\n  This can indicate older firmware; use the slow-SCK jumper if needed.\n";
        }
    } else {
        out << "  ISP clock: Auto scan will be resolved when target mode starts.\n";
    }

    if (!parser.isSet(targetOption)) {
        out << "PASS: programmer-only test completed. No target command was sent.\n";
        return 0;
    }

    AvrIspProgrammer programmer(usbasp);
    QString warning;
    bool started = programmer.begin(clock, warning, error);
    bool tpiMode = false;
    if (!started) {
        QString tpiWarning;
        QString tpiError;
        started = programmer.beginTpi(clock, tpiWarning, tpiError);
        if (started) {
            warning = tpiWarning;
            tpiMode = true;
        } else if (error.isEmpty()) {
            error = tpiError;
        }
    }
    if (!started) {
        err << "FAIL: USBasp opened, but target programming entry failed: " << error << '\n';
        return 4;
    }
    if (!warning.isEmpty()) {
        out << "WARNING: " << warning << '\n';
    }

    QByteArray signature;
    if (!programmer.readSignature(signature, error)) {
        err << "FAIL: target signature read failed: " << error << '\n';
        return 5;
    }
    programmer.end();

    out << "PASS: target entered " << (tpiMode ? "TPI" : "SPI ISP") << " mode\n"
        << "  Signature: " << signatureText(signature) << '\n'
        << "No erase, flash write, EEPROM write, fuse write, or lock write was performed.\n";
    return 0;
}

// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrDatabase.h"
#include "avr/AvrIspProgrammer.h"
#include "firmware/IntelHex.h"
#include "usb/UsbAspDevice.h"
#include "usb/UsbAspProtocol.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

std::atomic_bool g_cancelRequested = false;

enum class FileFormat {
  Auto,
  IntelHex,
  Raw,
  Immediate,
  HexText
};

enum class UpdateAction {
  Read,
  Write,
  Verify
};

struct UpdateSpec {
  QString memory;
  UpdateAction action = UpdateAction::Read;
  QString fileName;
  FileFormat format = FileFormat::Auto;
  QString original;
};

struct CliOptions {
  QString programmer = QStringLiteral("usbasp");
  QString part;
  QString port;
  QString sck;
  QString bitClock;
  QStringList updateSpecs;
  bool help = false;
  bool version = false;
  bool listParts = false;
  bool detect = false;
  bool signature = false;
  bool erase = false;
  bool disableAutoErase = false;
  bool disableVerify = false;
  bool dryRun = false;
  bool quiet = false;
  int verbose = 0;
};

void handleSignal(int) {
  g_cancelRequested.store(true);
}

QString hexBytes(QByteArrayView bytes) {
  return QString::fromLatin1(QByteArray(bytes.data(), bytes.size()).toHex(' ').toUpper());
}

void printHelp(QTextStream& out) {
  out << "FlyingBytesProCLI V3.2.20 - direct libusb USBasp AVR programmer\n\n"
      << "Usage:\n"
      << "  FlyingBytesProCLI -c usbasp -p <part> [options] -U <memory>:<op>:<file>[:format]\n\n"
      << "AVRDUDE-style options:\n"
      << "  -c, --programmer <id>   Programmer ID; usbasp is currently supported\n"
      << "  -p, --part <id>         MCU ID, for example atmega16 or atmega328p\n"
      << "  -U <spec>               Memory operation; may be repeated\n"
      << "  -e                       Chip erase before memory operations\n"
      << "  -D                       Disable automatic erase before Flash write\n"
      << "  -V                       Disable automatic verification after write\n"
      << "  -B <value>               ISP bit-clock period in us, or frequency with Hz/kHz/MHz\n"
      << "  -P <port>                Accepts usb for command compatibility\n"
      << "  -n                       Dry run; connect and validate, but do not modify MCU\n"
      << "  -v                       Verbose output; repeat for USB block tracing\n"
      << "  -q                       Quiet status output\n\n"
      << "FlyingBytesPro options:\n"
      << "  --sck <value>            pro, auto, 3m, 1.5m, 750k, 375k, 187.5k, 93.75k,\n"
      << "                           32k, 16k, 8k, 4k, 2k, 1k, or 500\n"
      << "                           Pro is the default and uses the USBasp firmware clock.\n"
      << "  --detect                 Read signature and show matching MCU records\n"
      << "  --signature              Print the target signature\n"
      << "  --list-parts             List all embedded MCU IDs\n"
      << "  -h, --help               Show this help\n"
      << "  --version                Show version\n\n"
      << "Memories:\n"
      << "  flash, eeprom, lfuse, hfuse, efuse, lock, signature\n\n"
      << "Operations:\n"
      << "  r = read, w = write, v = verify\n\n"
      << "Formats:\n"
      << "  i = Intel HEX, r = raw binary, m = immediate value, h = hex text,\n"
      << "  a = automatic from file extension\n\n"
      << "Examples:\n"
      << "  FlyingBytesProCLI -c usbasp -p atmega16 -U flash:w:firmware.hex:i\n"
      << "  FlyingBytesProCLI -c usbasp -p atmega16 -U flash:r:backup.hex:i\n"
      << "  FlyingBytesProCLI -c usbasp -p atmega16 -U flash:v:firmware.hex:i\n"
      << "  FlyingBytesProCLI -c usbasp -p atmega16 -U lfuse:w:0xE1:m -U hfuse:w:0x99:m\n"
      << "  FlyingBytesProCLI -c usbasp -p auto --detect\n";
}

bool takeValue(const QStringList& args, int& index, const QString& option,
               QString& value, QString& error) {
  if (index + 1 >= args.size()) {
    error = QStringLiteral("Option %1 requires a value.").arg(option);
    return false;
  }
  value = args.at(++index);
  return true;
}

bool parseArguments(const QStringList& args, CliOptions& options, QString& error) {
  for (int i = 1; i < args.size(); ++i) {
    const QString arg = args.at(i);
    if (arg == QStringLiteral("-h") || arg == QStringLiteral("--help")) {
      options.help = true;
    } else if (arg == QStringLiteral("--version")) {
      options.version = true;
    } else if (arg == QStringLiteral("--list-parts")) {
      options.listParts = true;
    } else if (arg == QStringLiteral("--detect")) {
      options.detect = true;
    } else if (arg == QStringLiteral("--signature")) {
      options.signature = true;
    } else if (arg == QStringLiteral("-e")) {
      options.erase = true;
    } else if (arg == QStringLiteral("-D")) {
      options.disableAutoErase = true;
    } else if (arg == QStringLiteral("-V")) {
      options.disableVerify = true;
    } else if (arg == QStringLiteral("-n")) {
      options.dryRun = true;
    } else if (arg == QStringLiteral("-q")) {
      options.quiet = true;
    } else if (arg == QStringLiteral("-v")) {
      ++options.verbose;
    } else if (arg == QStringLiteral("-c") || arg == QStringLiteral("--programmer")) {
      if (!takeValue(args, i, arg, options.programmer, error)) {
        return false;
      }
    } else if (arg.startsWith(QStringLiteral("-c")) && arg.size() > 2) {
      options.programmer = arg.mid(2);
    } else if (arg == QStringLiteral("-p") || arg == QStringLiteral("--part")) {
      if (!takeValue(args, i, arg, options.part, error)) {
        return false;
      }
    } else if (arg.startsWith(QStringLiteral("-p")) && arg.size() > 2) {
      options.part = arg.mid(2);
    } else if (arg == QStringLiteral("-U")) {
      QString value;
      if (!takeValue(args, i, arg, value, error)) {
        return false;
      }
      options.updateSpecs.append(value);
    } else if (arg.startsWith(QStringLiteral("-U")) && arg.size() > 2) {
      options.updateSpecs.append(arg.mid(2));
    } else if (arg == QStringLiteral("-P") || arg == QStringLiteral("--port")) {
      if (!takeValue(args, i, arg, options.port, error)) {
        return false;
      }
    } else if (arg.startsWith(QStringLiteral("-P")) && arg.size() > 2) {
      options.port = arg.mid(2);
    } else if (arg == QStringLiteral("-B")) {
      if (!takeValue(args, i, arg, options.bitClock, error)) {
        return false;
      }
    } else if (arg.startsWith(QStringLiteral("-B")) && arg.size() > 2) {
      options.bitClock = arg.mid(2);
    } else if (arg == QStringLiteral("--sck")) {
      if (!takeValue(args, i, arg, options.sck, error)) {
        return false;
      }
    } else {
      error = QStringLiteral("Unknown option: %1").arg(arg);
      return false;
    }
  }

  if (!options.sck.isEmpty() && !options.bitClock.isEmpty()) {
    error = QStringLiteral("Use either --sck or -B, not both.");
    return false;
  }
  return true;
}

FileFormat parseFormat(const QString& text, bool& ok) {
  const QString value = text.trimmed().toLower();
  ok = true;
  if (value.isEmpty() || value == QStringLiteral("a")) {
    return FileFormat::Auto;
  }
  if (value == QStringLiteral("i")) {
    return FileFormat::IntelHex;
  }
  if (value == QStringLiteral("r")) {
    return FileFormat::Raw;
  }
  if (value == QStringLiteral("m")) {
    return FileFormat::Immediate;
  }
  if (value == QStringLiteral("h")) {
    return FileFormat::HexText;
  }
  ok = false;
  return FileFormat::Auto;
}

bool parseUpdateSpec(const QString& text, UpdateSpec& spec, QString& error) {
  const int first = text.indexOf(QLatin1Char(':'));
  const int second = first < 0 ? -1 : text.indexOf(QLatin1Char(':'), first + 1);
  if (first <= 0 || second <= first + 1 || second >= text.size() - 1) {
    error = QStringLiteral("Invalid -U specification: %1").arg(text);
    return false;
  }

  spec.original = text;
  spec.memory = text.left(first).trimmed().toLower();
  const QString actionText = text.mid(first + 1, second - first - 1).trimmed().toLower();
  QString remainder = text.mid(second + 1);

  if (actionText == QStringLiteral("r")) {
    spec.action = UpdateAction::Read;
  } else if (actionText == QStringLiteral("w")) {
    spec.action = UpdateAction::Write;
  } else if (actionText == QStringLiteral("v")) {
    spec.action = UpdateAction::Verify;
  } else {
    error = QStringLiteral("Unsupported -U operation '%1' in %2.").arg(actionText, text);
    return false;
  }

  spec.format = FileFormat::Auto;
  const int finalColon = remainder.lastIndexOf(QLatin1Char(':'));
  if (finalColon >= 0 && finalColon + 2 == remainder.size()) {
    bool formatOk = false;
    const FileFormat format = parseFormat(remainder.mid(finalColon + 1), formatOk);
    if (formatOk) {
      spec.format = format;
      remainder.truncate(finalColon);
    }
  }

  spec.fileName = remainder;
  if (spec.fileName.isEmpty()) {
    error = QStringLiteral("The -U specification has an empty file or value: %1").arg(text);
    return false;
  }

  static const QStringList memories = {
    QStringLiteral("flash"), QStringLiteral("eeprom"),
    QStringLiteral("fuse"), QStringLiteral("lfuse"),
    QStringLiteral("hfuse"), QStringLiteral("efuse"),
    QStringLiteral("lock"), QStringLiteral("signature")
  };
  if (!memories.contains(spec.memory)) {
    error = QStringLiteral("Unsupported memory '%1' in %2.").arg(spec.memory, text);
    return false;
  }
  return true;
}

usbasp::IspClock clockForFrequency(double requestedHz) {
  const usbasp::IspClock clocks[] = {
    usbasp::IspClock::MHz3,
    usbasp::IspClock::MHz1_5,
    usbasp::IspClock::KHz750,
    usbasp::IspClock::KHz375,
    usbasp::IspClock::KHz187_5,
    usbasp::IspClock::KHz93_75,
    usbasp::IspClock::KHz32,
    usbasp::IspClock::KHz16,
    usbasp::IspClock::KHz8,
    usbasp::IspClock::KHz4,
    usbasp::IspClock::KHz2,
    usbasp::IspClock::KHz1,
    usbasp::IspClock::Hz500
  };
  for (const usbasp::IspClock clock : clocks) {
    if (usbasp::clockFrequencyHz(clock) <= requestedHz) {
      return clock;
    }
  }
  return usbasp::IspClock::Hz500;
}

bool parseSck(const CliOptions& options, usbasp::IspClock& clock, QString& error) {
  if (!options.bitClock.isEmpty()) {
    QString value = options.bitClock.trimmed().toLower();
    value.remove(QLatin1Char(' '));
    double requestedHz = 0.0;
    bool ok = false;
    if (value.endsWith(QStringLiteral("mhz"))) {
      value.chop(3);
      requestedHz = value.toDouble(&ok) * 1'000'000.0;
    } else if (value.endsWith(QStringLiteral("khz"))) {
      value.chop(3);
      requestedHz = value.toDouble(&ok) * 1'000.0;
    } else if (value.endsWith(QStringLiteral("hz"))) {
      value.chop(2);
      requestedHz = value.toDouble(&ok);
    } else {
      const double microseconds = value.toDouble(&ok);
      if (ok && microseconds > 0.0) {
        requestedHz = 1'000'000.0 / microseconds;
      }
    }
    if (!ok || requestedHz <= 0.0) {
      error = QStringLiteral("Invalid -B bit-clock value: %1").arg(options.bitClock);
      return false;
    }
    clock = clockForFrequency(requestedHz);
    return true;
  }

  if (options.sck.isEmpty()) {
    clock = usbasp::IspClock::Pro;
    return true;
  }

  QString value = options.sck.trimmed().toLower();
  value.remove(QLatin1Char(' '));
  value.remove(QStringLiteral("hz"));
  if (value == QStringLiteral("auto")) {
    clock = usbasp::IspClock::Auto;
    return true;
  }
  if (value == QStringLiteral("pro")) {
    clock = usbasp::IspClock::Pro;
    return true;
  }

  const struct {
    const char* name;
    usbasp::IspClock clock;
  } names[] = {
    {"3m", usbasp::IspClock::MHz3},
    {"3mhz", usbasp::IspClock::MHz3},
    {"1.5m", usbasp::IspClock::MHz1_5},
    {"1.5mhz", usbasp::IspClock::MHz1_5},
    {"1500k", usbasp::IspClock::MHz1_5},
    {"750k", usbasp::IspClock::KHz750},
    {"375k", usbasp::IspClock::KHz375},
    {"187.5k", usbasp::IspClock::KHz187_5},
    {"93.75k", usbasp::IspClock::KHz93_75},
    {"32k", usbasp::IspClock::KHz32},
    {"16k", usbasp::IspClock::KHz16},
    {"8k", usbasp::IspClock::KHz8},
    {"4k", usbasp::IspClock::KHz4},
    {"2k", usbasp::IspClock::KHz2},
    {"1k", usbasp::IspClock::KHz1},
    {"500", usbasp::IspClock::Hz500}
  };
  for (const auto& entry : names) {
    if (value == QLatin1String(entry.name)) {
      clock = entry.clock;
      return true;
    }
  }

  error = QStringLiteral("Invalid --sck value: %1").arg(options.sck);
  return false;
}

FileFormat resolveFormat(FileFormat requested, const QString& fileName,
                         bool singleByteMemory, UpdateAction action) {
  if (requested != FileFormat::Auto) {
    return requested;
  }
  if (singleByteMemory) {
    return action == UpdateAction::Read ? FileFormat::HexText : FileFormat::Immediate;
  }
  const QString suffix = QFileInfo(fileName).suffix().toLower();
  if (suffix == QStringLiteral("hex") || suffix == QStringLiteral("ihx")
      || suffix == QStringLiteral("ihex")) {
    return FileFormat::IntelHex;
  }
  return FileFormat::Raw;
}

bool readAllInput(const QString& fileName, QByteArray& data, QString& error) {
  QFile file;
  if (fileName == QStringLiteral("-")) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    if (!file.open(stdin, QIODevice::ReadOnly)) {
      error = QStringLiteral("Cannot open standard input.");
      return false;
    }
  } else {
    file.setFileName(fileName);
    if (!file.open(QIODevice::ReadOnly)) {
      error = QStringLiteral("Cannot open %1: %2").arg(fileName, file.errorString());
      return false;
    }
  }
  data = file.readAll();
  error.clear();
  return true;
}

bool writeAllOutput(const QString& fileName, QByteArrayView data, bool binary,
                    QString& error) {
  QFile file;
  if (fileName == QStringLiteral("-")) {
#ifdef _WIN32
    if (binary) {
      _setmode(_fileno(stdout), _O_BINARY);
    }
#endif
    if (!file.open(stdout, QIODevice::WriteOnly)) {
      error = QStringLiteral("Cannot open standard output.");
      return false;
    }
  } else {
    file.setFileName(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      error = QStringLiteral("Cannot write %1: %2").arg(fileName, file.errorString());
      return false;
    }
  }
  if (file.write(data.data(), data.size()) != data.size()) {
    error = QStringLiteral("Output write failed: %1").arg(file.errorString());
    return false;
  }
  file.flush();
  error.clear();
  return true;
}

bool loadImage(const UpdateSpec& spec, qsizetype capacity, FirmwareImage& image,
               QString& error) {
  const FileFormat format = resolveFormat(spec.format, spec.fileName, false, spec.action);
  QByteArray data;
  if (!readAllInput(spec.fileName, data, error)) {
    return false;
  }
  image.reset(capacity, 0xFF);
  if (format == FileFormat::IntelHex) {
    return IntelHex::parse(data, image, error);
  }
  if (format == FileFormat::Raw) {
    image = FirmwareImage::fromBinary(data, capacity, error, 0xFF);
    return image.capacity() == capacity && error.isEmpty();
  }
  error = QStringLiteral("Flash and EEPROM input support Intel HEX (:i) or raw binary (:r).");
  return false;
}

bool saveImage(const UpdateSpec& spec, const FirmwareImage& image, QString& error) {
  const FileFormat format = resolveFormat(spec.format, spec.fileName, false, spec.action);
  if (format == FileFormat::IntelHex) {
    const QByteArray data = IntelHex::serialize(image);
    return writeAllOutput(spec.fileName, data, false, error);
  }
  if (format == FileFormat::Raw) {
    const QByteArray data = image.toBinary();
    return writeAllOutput(spec.fileName, data, true, error);
  }
  if (format == FileFormat::HexText) {
    const QByteArray data = image.toBinary().toHex(' ').toUpper() + '\n';
    return writeAllOutput(spec.fileName, data, false, error);
  }
  error = QStringLiteral("Flash and EEPROM output support Intel HEX (:i), raw binary (:r), or hex text (:h).");
  return false;
}

bool parseImmediateByte(const QString& text, quint8& value, QString& error) {
  QString normalized = text.trimmed();
  int base = 10;
  if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    normalized.remove(0, 2);
    base = 16;
  }
  bool ok = false;
  const uint parsed = normalized.toUInt(&ok, base);
  if (!ok || parsed > 0xFF) {
    error = QStringLiteral("Invalid byte value: %1").arg(text);
    return false;
  }
  value = static_cast<quint8>(parsed);
  return true;
}

bool loadSingleByte(const UpdateSpec& spec, quint8& value, QString& error) {
  const FileFormat format = resolveFormat(spec.format, spec.fileName, true, spec.action);
  if (format == FileFormat::Immediate) {
    return parseImmediateByte(spec.fileName, value, error);
  }

  QByteArray data;
  if (!readAllInput(spec.fileName, data, error)) {
    return false;
  }
  if (format == FileFormat::Raw) {
    if (data.size() != 1) {
      error = QStringLiteral("Raw fuse or lock input must contain exactly one byte.");
      return false;
    }
    value = static_cast<quint8>(data.at(0));
    return true;
  }
  if (format == FileFormat::HexText) {
    return parseImmediateByte(QString::fromLatin1(data).trimmed(), value, error);
  }
  if (format == FileFormat::IntelHex) {
    FirmwareImage image(1, 0xFF);
    if (!IntelHex::parse(data, image, error) || !image.isDefined(0)) {
      if (error.isEmpty()) {
        error = QStringLiteral("Intel HEX fuse input does not define address zero.");
      }
      return false;
    }
    value = image.byteAt(0);
    return true;
  }
  error = QStringLiteral("Unsupported single-byte input format.");
  return false;
}

bool saveBytes(const UpdateSpec& spec, QByteArrayView bytes, QString& error) {
  const bool singleByte = bytes.size() == 1;
  const FileFormat format = resolveFormat(spec.format, spec.fileName, singleByte, spec.action);
  if (format == FileFormat::Raw) {
    return writeAllOutput(spec.fileName, bytes, true, error);
  }
  if (format == FileFormat::HexText || format == FileFormat::Immediate) {
    QByteArray text;
    if (singleByte) {
      text = QByteArrayLiteral("0x") + QByteArray(bytes.data(), bytes.size()).toHex().toUpper() + '\n';
    } else {
      text = QByteArray(bytes.data(), bytes.size()).toHex(' ').toUpper() + '\n';
    }
    return writeAllOutput(spec.fileName, text, false, error);
  }
  if (format == FileFormat::IntelHex) {
    FirmwareImage image(bytes.size(), 0xFF);
    image.setBytes(0, bytes, true);
    const QByteArray data = IntelHex::serialize(image);
    return writeAllOutput(spec.fileName, data, false, error);
  }
  error = QStringLiteral("Unsupported output format.");
  return false;
}

QString transferSummary(const QString& action, const AvrIspProgrammer& programmer) {
  const qsizetype bytes = programmer.lastTransferBytes();
  const qint64 elapsedMs = programmer.lastTransferElapsedMs();
  if (elapsedMs <= 0) {
    return QStringLiteral("%1 completed: %2 bytes.").arg(action).arg(bytes);
  }
  const double seconds = elapsedMs / 1000.0;
  const double rate = bytes / seconds;
  const QString rateText = rate >= 1024.0
    ? QStringLiteral("%1 KiB/s").arg(rate / 1024.0, 0, 'f', 1)
    : QStringLiteral("%1 B/s").arg(rate, 0, 'f', 0);
  return QStringLiteral("%1 completed: %2 bytes in %3 s (%4).")
    .arg(action).arg(bytes).arg(seconds, 0, 'f', 3).arg(rateText);
}

class ProgressPrinter {
public:
  ProgressPrinter(QTextStream& stream, QString label, bool enabled)
      : m_stream(stream), m_label(std::move(label)), m_enabled(enabled) {
  }

  bool update(qsizetype completed, qsizetype total) {
    if (g_cancelRequested.load()) {
      return false;
    }
    if (!m_enabled || total <= 0) {
      return true;
    }
    const int percent = static_cast<int>((completed * 100) / total);
    if (percent != m_lastPercent) {
      m_stream << '\r' << m_label << ": " << percent << "%" << Qt::flush;
      m_lastPercent = percent;
    }
    return true;
  }

  void finish() {
    if (m_enabled && m_lastPercent >= 0) {
      m_stream << '\n';
    }
  }

private:
  QTextStream& m_stream;
  QString m_label;
  bool m_enabled = true;
  int m_lastPercent = -1;
};

int fuseIndex(const QString& memory) {
  if (memory == QStringLiteral("fuse") || memory == QStringLiteral("lfuse")) {
    return 0;
  }
  if (memory == QStringLiteral("hfuse")) {
    return 1;
  }
  if (memory == QStringLiteral("efuse")) {
    return 2;
  }
  return -1;
}

bool needsDevice(const UpdateSpec& spec) {
  return spec.memory != QStringLiteral("signature");
}

} // namespace

int main(int argc, char* argv[]) {
  QCoreApplication application(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("FlyingBytesProCLI"));
  QCoreApplication::setApplicationVersion(QStringLiteral("V3.2.20"));

  QTextStream out(stdout);
  QTextStream err(stderr);

  CliOptions options;
  QString error;
  if (!parseArguments(QCoreApplication::arguments(), options, error)) {
    err << "ERROR: " << error << "\n\n";
    printHelp(err);
    return 2;
  }
  if (options.help) {
    printHelp(out);
    return 0;
  }
  if (options.version) {
    out << "FlyingBytesProCLI V3.2.20\n";
    return 0;
  }

  AvrDatabase database;
  if (!database.load(error)) {
    err << "ERROR: " << error << '\n';
    return 3;
  }

  if (options.listParts || options.part == QStringLiteral("?")) {
    out << "MCU ID\tName\tInterface\tSignature\tFlash\tEEPROM\n";
    for (const AvrDevice& device : database.devices()) {
      out << device.id << '\t' << device.name << '\t'
          << (device.isTpi() ? QStringLiteral("TPI") : QStringLiteral("SPI ISP")) << '\t'
          << device.signatureText() << '\t' << device.flashSize << '\t'
          << device.eepromSize << '\n';
    }
    return 0;
  }

  if (options.programmer == QStringLiteral("?")) {
    out << "usbasp\tUSBasp direct libusb backend\n";
    return 0;
  }
  if (options.programmer.compare(QStringLiteral("usbasp"), Qt::CaseInsensitive) != 0) {
    err << "ERROR: Only the usbasp programmer is supported.\n";
    return 2;
  }
  if (!options.port.isEmpty()
      && !options.port.startsWith(QStringLiteral("usb"), Qt::CaseInsensitive)) {
    err << "ERROR: USBasp accepts only -P usb in this version.\n";
    return 2;
  }

  QVector<UpdateSpec> updates;
  updates.reserve(options.updateSpecs.size());
  for (const QString& text : options.updateSpecs) {
    UpdateSpec spec;
    if (!parseUpdateSpec(text, spec, error)) {
      err << "ERROR: " << error << '\n';
      return 2;
    }
    updates.append(spec);
  }

  usbasp::IspClock clock = usbasp::IspClock::Pro;
  if (!parseSck(options, clock, error)) {
    err << "ERROR: " << error << '\n';
    return 2;
  }

  const bool automaticPart = options.part.isEmpty()
    || options.part.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
  const AvrDevice* requestedDevice = nullptr;
  if (!automaticPart) {
    requestedDevice = database.byId(options.part);
    if (requestedDevice == nullptr) {
      err << "ERROR: Unknown MCU ID: " << options.part << '\n';
      return 2;
    }
  }

  std::signal(SIGINT, handleSignal);

  UsbAspDevice usbasp;
  if (options.verbose > 0) {
    usbasp.setTraceCallback([&err](const QString& message) {
      err << "USB: " << message << '\n' << Qt::flush;
    });
    usbasp.setDetailedBulkTracing(options.verbose > 1);
  }
  if (!usbasp.open(error)) {
    err << "ERROR: " << error << '\n';
    return 4;
  }

  if (!options.quiet) {
    const auto& info = usbasp.info();
    err << "USBasp "
        << QStringLiteral("%1:%2")
             .arg(info.vid, 4, 16, QLatin1Char('0'))
             .arg(info.pid, 4, 16, QLatin1Char('0')).toUpper()
        << " opened.\n";
  }

  AvrIspProgrammer programmer(usbasp);
  QString warning;
  bool started = requestedDevice != nullptr
    ? programmer.begin(*requestedDevice, clock, warning, error)
    : programmer.begin(clock, warning, error);
  if (!started && requestedDevice == nullptr) {
    QString tpiWarning;
    QString tpiError;
    started = programmer.beginTpi(clock, tpiWarning, tpiError);
    if (started) warning = tpiWarning;
    else if (error.isEmpty()) error = tpiError;
  }
  if (!started) {
    err << "ERROR: " << error << '\n';
    return 5;
  }
  if (!warning.isEmpty() && !options.quiet) {
    err << "WARNING: " << warning << '\n';
  }

  QByteArray signature;
  if (!programmer.readSignature(signature, error)) {
    err << "ERROR: " << error << '\n';
    return 5;
  }

  const QVector<const AvrDevice*> matches =
    database.bySignatureDetectionCandidates(signature);
  if (options.detect) {
    out << "Signature: " << hexBytes(signature) << '\n';
    if (matches.isEmpty()) {
      out << "No MCU record matches this signature.\n";
    } else {
      out << "Matching MCU records:\n";
      for (const AvrDevice* match : matches) {
        out << "  " << match->id << "  " << match->name << '\n';
      }
    }
  }
  if (options.signature) {
    out << hexBytes(signature) << '\n';
  }

  const AvrDevice* device = requestedDevice;
  if (automaticPart && matches.size() == 1) {
    device = matches.first();
  } else if (matches.size() > 1) {
    bool requiresPart = options.erase;
    for (const UpdateSpec& spec : updates) {
      requiresPart = requiresPart || needsDevice(spec);
    }
    if (requiresPart) {
      err << "ERROR: Signature " << hexBytes(signature)
          << " matches multiple MCU records. Specify one with -p:\n";
      for (const AvrDevice* match : matches) {
        err << "  -p " << match->id << "  (" << match->name << ")\n";
      }
      return 6;
    }
  }

  if (device != nullptr && device->signature != signature) {
    err << "ERROR: Selected " << device->name << " expects signature "
        << device->signatureText() << ", but the target returned "
        << hexBytes(signature) << ".\n";
    return 6;
  }

  if (device != nullptr && !options.quiet) {
    err << "Target: " << device->name << " (" << device->id << "), signature "
        << device->signatureText() << ".\n";
  }

  if (options.erase) {
    if (device == nullptr) {
      err << "ERROR: -e requires a unique MCU selection.\n";
      return 2;
    }
    if (options.dryRun) {
      if (!options.quiet) {
        err << "DRY RUN: chip erase skipped.\n";
      }
    } else if (!programmer.chipErase(*device, error)) {
      err << "ERROR: Chip erase failed: " << error << '\n';
      return 7;
    } else if (!options.quiet) {
      err << "Chip erase completed.\n";
    }
  }

  bool autoEraseAvailable = !options.disableAutoErase && !options.erase;
  for (const UpdateSpec& spec : updates) {
    if (g_cancelRequested.load()) {
      err << "ERROR: Operation cancelled.\n";
      return 130;
    }

    if (spec.memory == QStringLiteral("signature")) {
      if (spec.action != UpdateAction::Read) {
        err << "ERROR: Signature memory is read-only.\n";
        return 2;
      }
      if (!saveBytes(spec, signature, error)) {
        err << "ERROR: " << error << '\n';
        return 8;
      }
      continue;
    }

    if (device == nullptr) {
      err << "ERROR: Memory operation requires a unique MCU selection.\n";
      return 2;
    }

    if (spec.memory == QStringLiteral("flash") || spec.memory == QStringLiteral("eeprom")) {
      const bool flash = spec.memory == QStringLiteral("flash");
      const qsizetype capacity = flash ? device->flashSize : device->eepromSize;
      if (capacity <= 0) {
        err << "ERROR: " << device->name << " has no " << spec.memory << " definition.\n";
        return 2;
      }

      ProgressPrinter progress(err,
        QStringLiteral("%1 %2")
          .arg(flash ? QStringLiteral("Flash") : QStringLiteral("EEPROM"))
          .arg(spec.action == UpdateAction::Read ? QStringLiteral("read")
               : spec.action == UpdateAction::Write ? QStringLiteral("write")
               : QStringLiteral("verify")),
        !options.quiet);

      if (spec.action == UpdateAction::Read) {
        FirmwareImage image;
        const bool ok = flash
          ? programmer.readFlash(*device, image, true,
              [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error)
          : programmer.readEeprom(*device, image,
              [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error);
        progress.finish();
        if (!ok) {
          err << "ERROR: " << error << '\n';
          return 7;
        }
        if (!saveImage(spec, image, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }
        if (!options.quiet) {
          err << transferSummary(flash ? QStringLiteral("Flash read")
                                      : QStringLiteral("EEPROM read"), programmer) << '\n';
        }
      } else {
        FirmwareImage image;
        if (!loadImage(spec, capacity, image, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }

        if (spec.action == UpdateAction::Verify) {
          const bool ok = flash
            ? programmer.verifyFlash(*device, image,
                [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error)
            : programmer.verifyEeprom(*device, image,
                [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error);
          progress.finish();
          if (!ok) {
            err << "ERROR: Verification failed: " << error << '\n';
            return 9;
          }
          if (!options.quiet) {
            err << (flash ? "Flash" : "EEPROM") << " verification passed.\n";
          }
          continue;
        }

        if (options.dryRun) {
          progress.finish();
          if (!options.quiet) {
            err << "DRY RUN: " << (flash ? "Flash" : "EEPROM")
                << " write skipped; " << image.definedCount() << " input bytes validated.\n";
          }
          continue;
        }

        const bool eraseFirst = flash && autoEraseAvailable;
        const bool ok = flash
          ? programmer.writeFlash(*device, image, eraseFirst,
              [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error)
          : programmer.writeEeprom(*device, image,
              [&progress](qsizetype done, qsizetype total) { return progress.update(done, total); }, error);
        progress.finish();
        if (!ok) {
          err << "ERROR: Write failed: " << error << '\n';
          return 7;
        }
        if (flash && eraseFirst) {
          autoEraseAvailable = false;
        }
        if (!options.quiet) {
          err << transferSummary(flash ? QStringLiteral("Flash write")
                                      : QStringLiteral("EEPROM write"), programmer) << '\n';
        }

        if (!options.disableVerify) {
          ProgressPrinter verifyProgress(err,
            flash ? QStringLiteral("Flash verify") : QStringLiteral("EEPROM verify"),
            !options.quiet);
          const bool verified = flash
            ? programmer.verifyFlash(*device, image,
                [&verifyProgress](qsizetype done, qsizetype total) { return verifyProgress.update(done, total); }, error)
            : programmer.verifyEeprom(*device, image,
                [&verifyProgress](qsizetype done, qsizetype total) { return verifyProgress.update(done, total); }, error);
          verifyProgress.finish();
          if (!verified) {
            err << "ERROR: Post-write verification failed: " << error << '\n';
            return 9;
          }
          if (!options.quiet) {
            err << (flash ? "Flash" : "EEPROM") << " verification passed.\n";
          }
        }
      }
      continue;
    }

    if (spec.memory == QStringLiteral("lock")) {
      if (spec.action == UpdateAction::Read) {
        quint8 value = 0;
        if (!programmer.readLock(*device, value, error)) {
          err << "ERROR: " << error << '\n';
          return 7;
        }
        const QByteArray byte(1, static_cast<char>(value));
        if (!saveBytes(spec, byte, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }
      } else {
        quint8 requested = 0;
        if (!loadSingleByte(spec, requested, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }
        if (spec.action == UpdateAction::Verify) {
          quint8 actual = 0;
          if (!programmer.readLock(*device, actual, error)) {
            err << "ERROR: " << error << '\n';
            return 7;
          }
          const quint8 mask = device->lockProgramMask;
          if ((actual & mask) != (requested & mask)) {
            err << "ERROR: Lock verification failed: expected 0x"
                << QStringLiteral("%1").arg(requested, 2, 16, QLatin1Char('0')).toUpper()
                << ", read 0x"
                << QStringLiteral("%1").arg(actual, 2, 16, QLatin1Char('0')).toUpper()
                << ".\n";
            return 9;
          }
        } else if (options.dryRun) {
          if (!options.quiet) {
            err << "DRY RUN: lock write skipped.\n";
          }
        } else if (!programmer.writeLock(*device, requested, error)) {
          err << "ERROR: " << error << '\n';
          return 7;
        }
      }
      continue;
    }

    const int index = fuseIndex(spec.memory);
    if (index >= 0) {
      if (index >= device->fuseCount()) {
        err << "ERROR: " << device->name << " does not define " << spec.memory << ".\n";
        return 2;
      }
      if (spec.action == UpdateAction::Read) {
        QVector<quint8> values;
        if (!programmer.readFuses(*device, values, error)) {
          err << "ERROR: " << error << '\n';
          return 7;
        }
        const QByteArray byte(1, static_cast<char>(values.at(index)));
        if (!saveBytes(spec, byte, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }
      } else {
        quint8 requested = 0;
        if (!loadSingleByte(spec, requested, error)) {
          err << "ERROR: " << error << '\n';
          return 8;
        }
        if (spec.action == UpdateAction::Verify) {
          QVector<quint8> values;
          if (!programmer.readFuses(*device, values, error)) {
            err << "ERROR: " << error << '\n';
            return 7;
          }
          const quint8 mask = device->fuseProgramMasks.at(index);
          if ((values.at(index) & mask) != (requested & mask)) {
            err << "ERROR: " << spec.memory << " verification failed: expected 0x"
                << QStringLiteral("%1").arg(requested, 2, 16, QLatin1Char('0')).toUpper()
                << ", read 0x"
                << QStringLiteral("%1").arg(values.at(index), 2, 16, QLatin1Char('0')).toUpper()
                << ".\n";
            return 9;
          }
        } else if (options.dryRun) {
          if (!options.quiet) {
            err << "DRY RUN: " << spec.memory << " write skipped.\n";
          }
        } else {
          QVector<int> requestedValues(device->fuseCount(), -1);
          requestedValues[index] = requested;
          if (!programmer.writeFuses(*device, requestedValues, error)) {
            err << "ERROR: " << error << '\n';
            return 7;
          }
        }
      }
      continue;
    }
  }

  programmer.end();
  if (!options.quiet && updates.isEmpty() && !options.detect && !options.signature) {
    err << "USBasp and target communication passed. No memory operation requested.\n";
  }
  return 0;
}

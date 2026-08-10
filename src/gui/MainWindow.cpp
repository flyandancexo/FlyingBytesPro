// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/MainWindow.h"

#include "firmware/IntelHex.h"
#include "firmware/ProjectFile.h"
#include "gui/FuseLockDialog.h"
#include "gui/DisplayLanguage.h"
#include "gui/HexTableModel.h"
#include "gui/MemoryMeterButton.h"
#include "gui/ProgrammerController.h"
#include "gui/SckDial.h"
#include "gui/UsbAspDriverDialog.h"
#include "usb/UsbAspProtocol.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QColor>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSettings>
#include <QSizePolicy>
#include <QStringList>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStatusBar>
#include <QStyle>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

QString selectedTabBorderColorForIndex(int index) {
  switch (index) {
    case 0: return QStringLiteral("#FF9100"); // Task - neon orange
    case 1: return QStringLiteral("#469BD7"); // Flash - light blue
    case 2: return QStringLiteral("#48AA62"); // EEPROM - light green
    case 3: return QStringLiteral("#8967CA"); // Full Log - light purple
    case 4: return QStringLiteral("#BEBEBE"); // Settings - light grey
    case 5: return QStringLiteral("#C6A47E"); // About - light brown
    default: return QStringLiteral("#2B7DB7");
  }
}

QString memoryName(bool flash) {
  return DisplayLanguage::text(flash ? QStringLiteral("Flash")
                                     : QStringLiteral("EEPROM"));
}

QString memorySizeText(quint32 bytes) {
  if (bytes >= 1024 && bytes % 1024 == 0) {
    return QStringLiteral("%1 KB").arg(bytes / 1024);
  }
  return QStringLiteral("%1 %2")
    .arg(bytes)
    .arg(DisplayLanguage::text(QStringLiteral("Bytes")));
}

QString deviceDetailsText(const AvrDevice& device) {
  return QStringLiteral(
    "<b>ID:</b> [%1]<br>"
    "<span style='color:#287bb8; font-weight:700;'>%2: %3</span>"
    "&nbsp;&nbsp;&nbsp;"
    "<span style='color:#2d9b57; font-weight:700;'>%4: %5</span>")
    .arg(device.signatureText(), DisplayLanguage::text(QStringLiteral("Flash")),
         memorySizeText(device.flashSize), DisplayLanguage::text(QStringLiteral("EEPROM")),
         memorySizeText(device.eepromSize));
}

bool isHexPath(const QString& path) {
  const QString lower = path.toLower();
  return lower.endsWith(QStringLiteral(".hex"))
    || lower.endsWith(QStringLiteral(".ihx"))
    || lower.endsWith(QStringLiteral(".ihex"));
}

QString byteText(int value) {
  return value < 0 ? QString() : QStringLiteral("%1")
    .arg(value & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
}

QList<qsizetype> selectedMemoryOffsets(QTableView* table, qsizetype capacity) {
  QList<qsizetype> offsets;
  if (!table || !table->selectionModel() || capacity <= 0) return offsets;

  for (const QModelIndex& index : table->selectionModel()->selectedIndexes()) {
    if (!index.isValid()) continue;
    const qsizetype rowBase = static_cast<qsizetype>(index.row()) * 16;
    if (index.column() >= 1 && index.column() <= 16) {
      const qsizetype offset = rowBase + index.column() - 1;
      if (offset >= 0 && offset < capacity) offsets.append(offset);
      continue;
    }
    if (index.column() == 18) {
      const qsizetype end = std::min(rowBase + 16, capacity);
      for (qsizetype offset = rowBase; offset < end; ++offset) {
        offsets.append(offset);
      }
    }
  }

  std::sort(offsets.begin(), offsets.end());
  offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
  return offsets;
}

QMessageBox::StandardButton showLocalizedMessageBox(
    QWidget* parent, QMessageBox::Icon icon, const QString& title,
    const QString& text, QMessageBox::StandardButtons buttons,
    QMessageBox::StandardButton defaultButton = QMessageBox::NoButton) {
  QMessageBox box(icon, DisplayLanguage::text(title), DisplayLanguage::text(text),
                  buttons, parent);
  if (defaultButton != QMessageBox::NoButton) box.setDefaultButton(defaultButton);
  const struct {
    QMessageBox::StandardButton button;
    const char* text;
  } labels[] = {
    {QMessageBox::Yes, "Yes"},
    {QMessageBox::No, "No"},
    {QMessageBox::Cancel, "Cancel"},
    {QMessageBox::Ok, "OK"}
  };
  for (const auto& label : labels) {
    if (QAbstractButton* button = box.button(label.button)) {
      button->setText(DisplayLanguage::text(QString::fromLatin1(label.text)));
    }
  }
  box.exec();
  return box.standardButton(box.clickedButton());
}

QPushButton* makeButton(const QString& text, const QString& icon,
                        QWidget* parent) {
  auto* button = new QPushButton(QIcon(icon), text, parent);
  button->setIconSize(QSize(20, 20));
  button->setMinimumHeight(30);
  return button;
}

void removeImageButtonOutline(QToolButton* button) {
  button->setFocusPolicy(Qt::NoFocus);
  button->setAutoRaise(false);
  button->setStyleSheet(QStringLiteral(
    "QToolButton { border: none; border-radius: 8px; background: transparent; padding: 0px; }"
    "QToolButton:hover { border: none; background: rgba(43, 125, 183, 24); }"
    "QToolButton:pressed { border: none; background: rgba(43, 125, 183, 48); padding-left: 2px; padding-top: 2px; }"
    "QToolButton:disabled { border: none; background: transparent; }"));
}

QToolButton* makeImageButton(const QString& text, const QString& icon,
                             QWidget* parent) {
  auto* button = new QToolButton(parent);
  button->setText(text);
  button->setIcon(QIcon(icon));
  button->setIconSize(QSize(54, 54));
  button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  button->setFixedSize(85, 85);
  removeImageButtonOutline(button);
  return button;
}

QColor mcuClassColor(const QString& name) {
  if (name.startsWith(QStringLiteral("ATtiny"), Qt::CaseInsensitive)) return QColor(39, 145, 82);
  if (name.startsWith(QStringLiteral("ATmega"), Qt::CaseInsensitive)) return QColor(42, 108, 190);
  if (name.startsWith(QStringLiteral("AT90"), Qt::CaseInsensitive)) return QColor(225, 133, 35);
  if (name.startsWith(QStringLiteral("ATA"), Qt::CaseInsensitive)) return QColor(135, 82, 174);
  return QColor(105, 115, 128);
}

QIcon mcuClassIcon(const QString& name) {
  QPixmap pixmap(18, 18);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);
  const QColor color = mcuClassColor(name);
  painter.setPen(QPen(color.darker(135), 1));
  painter.setBrush(color);
  painter.drawRoundedRect(QRectF(4.5, 3.5, 9.0, 11.0), 1.5, 1.5);
  painter.setPen(QPen(color.darker(150), 1.2));
  for (int y : {5, 9, 13}) {
    painter.drawLine(1, y, 4, y);
    painter.drawLine(14, y, 17, y);
  }
  painter.setPen(QPen(Qt::white, 1.2));
  painter.drawLine(7, 6, 11, 6);
  return QIcon(pixmap);
}

QString normalizedHexByte(QString text) {
  text = text.trimmed();
  if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) text.remove(0, 2);
  bool ok = false;
  const int value = text.toInt(&ok, 16);
  return ok && value >= 0 && value <= 0xFF
    ? QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper()
    : QString();
}

QString valuesText(const QVector<int>& values) {
  QStringList text;
  for (const int value : values) {
    text.append(value >= 0 ? byteText(value) : QStringLiteral("--"));
  }
  return text.join(QLatin1Char(' '));
}

QString aboutFeaturesText() {
  const QStringList featureLines = {
    QStringLiteral("Supports 175 AVR microcontrollers: 167 through SPI ISP plus 8 through TPI."),
    QStringLiteral("Sophisticated and beautiful GUI with a simple, focused design."),
    QStringLiteral("New AVRDUDE-style command-line interface using the USBasp protocol."),
    QStringLiteral("Automatic MCU detection using device-signature bytes."),
    QStringLiteral("Completely rewritten backend for direct USBasp programming through libusb."),
    QStringLiteral("Flash and EEPROM reading, writing, verification, and blank checking."),
    QStringLiteral("Smart and Full MCU Flash reads with automatic erased-tail cleanup."),
    QStringLiteral("Configurable automatic programming sequences and portable project files."),
    QStringLiteral("Editable hexadecimal and ASCII memory buffers."),
    QStringLiteral("Intel HEX and raw binary file loading and saving."),
    QStringLiteral("Auto SCK scans from the fastest supported clock downward; Pro uses the USBasp firmware default clock request."),
    QStringLiteral("Fuse and lock-byte reading, masked programming, and readback verification."),
    QStringLiteral("CRC-16 memory identification and detailed operation logging."),
    QStringLiteral("Open-source software licensed under GNU GPL-3.0-or-later.")
  };

  QString html = QStringLiteral("<b>%1</b><br>")
    .arg(DisplayLanguage::text(QStringLiteral("Key Features")));
  for (const QString& feature : featureLines) {
    html += QStringLiteral("&bull; %1<br>").arg(DisplayLanguage::text(feature));
  }
  if (html.endsWith(QStringLiteral("<br>"))) html.chop(4);
  return html;
}

QJsonObject fuseMetadataForDevice(const AvrDevice& device) {
  static const QJsonArray metadataDevices = [] {
    QFile file(QStringLiteral(":/devices/fuse_ui.json"));
    if (!file.open(QIODevice::ReadOnly)) return QJsonArray{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    return document.object().value(QStringLiteral("devices")).toArray();
  }();

  QJsonObject exactNameUnknownSignature;
  QJsonObject signatureFallback;
  const QByteArray unknownSignature(3, '\0');

  for (const QJsonValue& value : metadataDevices) {
    const QJsonObject object = value.toObject();
    const QByteArray signature = QByteArray::fromHex(
      object.value(QStringLiteral("signature")).toString().toLatin1());
    const bool exactName = object.value(QStringLiteral("name")).toString()
      .compare(device.name, Qt::CaseInsensitive) == 0;

    if (exactName && signature == device.signature) return object;

    // Some ProgISP 1.72 decoder records contain 000000 instead of a usable
    // device signature. An exact MCU-name match is safe to use as the
    // decoder source; never use an unknown-signature record for another MCU.
    if (exactName && signature == unknownSignature
        && exactNameUnknownSignature.isEmpty()) {
      exactNameUnknownSignature = object;
    }

    if (signature == device.signature && signatureFallback.isEmpty()) {
      signatureFallback = object;
    }
  }

  if (!exactNameUnknownSignature.isEmpty()) return exactNameUnknownSignature;
  return signatureFallback;
}

struct BootSectionChoice {
  quint32 sizeBytes = 0;
  quint32 startWordAddress = 0;
  int fuseByteIndex = -1;
  int mask = 0;
  int value = 0;
};

struct BootSectionMetadata {
  QVector<BootSectionChoice> choices;
  int resetFuseByteIndex = -1;
  int resetMask = 0;
  int resetEnabledValue = 0;
};

BootSectionMetadata bootSectionMetadataForDevice(const AvrDevice& device) {
  BootSectionMetadata result;
  const QJsonObject metadata = fuseMetadataForDevice(device);
  if (metadata.isEmpty() || device.flashSize < 2) return result;

  struct RawBootChoice {
    quint32 words = 0;
    int fuseByteIndex = -1;
    int mask = 0;
    int value = 0;
  };
  QVector<RawBootChoice> rawChoices;
  const QRegularExpression sizePattern(QStringLiteral(
    "Boot Flash section size=(\\d+) words"));

  const QJsonArray fuses = metadata.value(QStringLiteral("fuses")).toArray();
  for (const QJsonValue& fuseValue : fuses) {
    const QJsonObject fuse = fuseValue.toObject();
    const int byteIndex = fuse.value(QStringLiteral("byteIndex")).toInt(-1);

    const QJsonArray bits = fuse.value(QStringLiteral("bits")).toArray();
    for (const QJsonValue& bitValue : bits) {
      const QJsonObject bit = bitValue.toObject();
      if (bit.value(QStringLiteral("name")).toString()
          .compare(QStringLiteral("BOOTRST"), Qt::CaseInsensitive) == 0) {
        const int bitNumber = bit.value(QStringLiteral("bit")).toInt(-1);
        if (byteIndex >= 0 && bitNumber >= 0 && bitNumber <= 7) {
          result.resetFuseByteIndex = byteIndex;
          result.resetMask = 1 << bitNumber;
          result.resetEnabledValue = 0;
        }
      }
    }

    const QJsonArray options = fuse.value(QStringLiteral("options")).toArray();
    for (const QJsonValue& optionValue : options) {
      const QJsonObject option = optionValue.toObject();
      const QString text = option.value(QStringLiteral("text")).toString();
      const QRegularExpressionMatch match = sizePattern.match(text);
      if (match.hasMatch()) {
        bool ok = false;
        const quint32 words = match.captured(1).toUInt(&ok);
        if (ok && words > 0 && byteIndex >= 0) {
          rawChoices.append({
            words, byteIndex,
            option.value(QStringLiteral("mask")).toInt(),
            option.value(QStringLiteral("value")).toInt()
          });
        }
      }

      if (result.resetMask != 0
          && byteIndex == result.resetFuseByteIndex
          && text.contains(QStringLiteral("Boot Reset vector Enabled"),
                           Qt::CaseInsensitive)) {
        const int optionMask = option.value(QStringLiteral("mask")).toInt();
        if ((optionMask & result.resetMask) != 0) {
          result.resetEnabledValue =
            option.value(QStringLiteral("value")).toInt() & result.resetMask;
        }
      }
    }
  }

  std::sort(rawChoices.begin(), rawChoices.end(),
            [](const RawBootChoice& left, const RawBootChoice& right) {
    return left.words < right.words;
  });
  if (rawChoices.isEmpty()) return result;

  // The decoder source has a few historical text typos in larger boot-size
  // descriptions. BOOTSZ choices are geometric: derive the supported sizes
  // from the smallest valid entry while preserving each option's mask/value.
  const quint32 smallestWords = rawChoices.first().words;
  const quint32 flashWords = device.flashSize / 2;
  for (int i = 0; i < rawChoices.size(); ++i) {
    if (i >= 31) break;
    const quint32 words = smallestWords << i;
    if (words == 0 || words > flashWords) continue;
    const RawBootChoice& raw = rawChoices.at(i);
    result.choices.append({
      words * 2, flashWords - words, raw.fuseByteIndex, raw.mask, raw.value
    });
  }
  return result;
}

QString bootSectionSizeText(quint32 bytes) {
  if (bytes >= 1024 && (bytes % 1024) == 0) {
    return QStringLiteral("%1Kb").arg(bytes / 1024);
  }
  return QStringLiteral("%1b").arg(bytes);
}

QString bootSectionSupportText(const AvrDevice& device,
                               const BootSectionMetadata& metadata) {
  if (metadata.choices.isEmpty()) return QStringLiteral("<b>Bootloader:</b> n/a");
  const quint32 highestWordAddress = device.flashSize > 1
    ? (device.flashSize / 2) - 1 : 0;
  const int addressDigits = std::max(4,
    static_cast<int>(QString::number(highestWordAddress, 16).size()));
  QStringList items;
  items.reserve(metadata.choices.size());
  for (const BootSectionChoice& choice : metadata.choices) {
    const QString address = QStringLiteral("%1")
      .arg(choice.startWordAddress, addressDigits, 16, QLatin1Char('0')).toUpper();
    items.append(QStringLiteral("[%1 0x%2]")
      .arg(bootSectionSizeText(choice.sizeBytes), address));
  }
  return QStringLiteral("<b>Bootloader:</b> %1").arg(items.join(QLatin1Char(' ')));
}

} // namespace

MainWindow::MainWindow(bool demoMode, QWidget* parent)
    : QMainWindow(parent), m_demoMode(demoMode) {
  QSettings startupSettings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  DisplayLanguage::setCurrentCode(
    startupSettings.value(QStringLiteral("displayLanguage"), QStringLiteral("en")).toString());

  QString error;
  if (!m_database.load(error)) {
    showLocalizedMessageBox(this, QMessageBox::Critical,
      QStringLiteral("Device Database Error"), error, QMessageBox::Ok, QMessageBox::Ok);
  }

  buildUi();
  populateDevices();
  restoreTaskSelections();

  if (m_sckDial) {
    int savedClockId = static_cast<int>(usbasp::IspClock::Pro);
    if (startupSettings.contains(QStringLiteral("sckClockId"))) {
      savedClockId = startupSettings.value(QStringLiteral("sckClockId")).toInt();
      const int clockSchema = startupSettings.value(
        QStringLiteral("sckClockSchema"), 1).toInt();
      if (clockSchema < 2 && savedClockId == 0) {
        savedClockId = static_cast<int>(usbasp::IspClock::Auto);
      }
    }
    startupSettings.setValue(QStringLiteral("sckClockSchema"), 2);
    m_sckDial->setClockId(savedClockId);
  }

  m_controller = new ProgrammerController(m_demoMode);
  m_controller->moveToThread(&m_workerThread);
  connect(&m_workerThread, &QThread::finished,
          m_controller, &QObject::deleteLater);
  connectController();
  m_workerThread.start();

  setWindowTitle(m_demoMode
    ? QStringLiteral("FlyingBytesPro V3.2.29 [DEMO MODE]")
    : QStringLiteral("FlyingBytesPro V3.2.29"));
  setWindowIcon(QIcon(QStringLiteral(":/icons/app_256.png")));
  setMinimumSize(1230, 650);
  QSettings windowSettings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  const QByteArray savedGeometry = windowSettings.value(
    QStringLiteral("mainWindowGeometry")).toByteArray();
  const int windowLayoutSchema = windowSettings.value(
    QStringLiteral("mainWindowLayoutSchema"), 1).toInt();
  if (!savedGeometry.isEmpty()) {
    restoreGeometry(savedGeometry);
    if (windowLayoutSchema < 4 && width() < 1230) {
      resize(1230, height());
    }
  } else {
    resize(1230, 780);
  }
  windowSettings.setValue(QStringLiteral("mainWindowLayoutSchema"), 4);

  applyDisplayLanguage(QStringLiteral("en"));

  m_autoProbeTimer = new QTimer(this);
  m_autoProbeTimer->setInterval(1500);
  connect(m_autoProbeTimer, &QTimer::timeout,
          this, &MainWindow::autoProbeTick);

  if (m_demoMode) {
    updateUsbStatus(true, QStringLiteral("Demo USBasp"));
    appendLog(QStringLiteral("Demo mode is active. No USB transfers will be made."));
    setMiniLog(QStringLiteral("Demo mode ready — no hardware access"), true);
  } else {
    updateUsbStatus(false, QStringLiteral("Searching..."));
    m_autoProbeTimer->start();
    QTimer::singleShot(150, this, &MainWindow::autoProbeTick);
  }
}

MainWindow::~MainWindow() {
  if (m_controller) {
    m_controller->requestCancel();
  }
  m_workerThread.quit();
  m_workerThread.wait();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.setValue(QStringLiteral("mainWindowGeometry"), saveGeometry());
  settings.setValue(QStringLiteral("selectedDeviceId"),
                    m_deviceCombo ? m_deviceCombo->currentData().toString() : QString());
  settings.setValue(QStringLiteral("sckClockId"), currentClockId());
  settings.setValue(QStringLiteral("sckClockSchema"), 2);
  saveTaskSelections();
  const AvrDevice* device = currentDevice();
  if (device) saveFuseValuesForDeviceId(device->id);
  QMainWindow::closeEvent(event);
}

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* mainLayout = new QVBoxLayout(central);
  mainLayout->setContentsMargins(6, 5, 6, 5);
  mainLayout->setSpacing(4);

  mainLayout->addWidget(buildTopStrip(central));

  m_tabs = new QTabWidget(central);
  m_tabs->setDocumentMode(false);
  m_tabs->setMovable(false);
  m_tabs->addTab(createTaskPage(m_tabs), QStringLiteral("Task"));
  m_flashUi = createMemoryPage(QStringLiteral("Flash"), true, m_tabs);
  m_eepromUi = createMemoryPage(QStringLiteral("EEPROM"), false, m_tabs);
  m_tabs->addTab(m_flashUi.page, mcuClassIcon(QStringLiteral("ATmega")),
                 QStringLiteral("Flash"));
  m_tabs->addTab(m_eepromUi.page, mcuClassIcon(QStringLiteral("ATtiny")),
                 QStringLiteral("EEPROM"));
  const int fullLogTabIndex = m_tabs->addTab(
    createFullLogPage(m_tabs), QIcon(QStringLiteral(":/icons/full_log.png")),
    QStringLiteral("Full Log"));
  m_tabs->addTab(createSettingsPage(m_tabs),
                 QIcon(QStringLiteral(":/icons/settings.png")),
                 QStringLiteral("Settings"));
  m_tabs->addTab(createAboutPage(m_tabs),
                 QIcon(QStringLiteral(":/icons/about.png")),
                 QStringLiteral("About"));

  // Keep every tab at an identical outer width in all selection states.
  // The content box is fixed at 76 px; selected 2 px side borders are
  // compensated by 16 px horizontal padding versus 17 px when unselected.
  const auto applySelectedTabBorderColor = [this](int index) {
    const QString color = selectedTabBorderColorForIndex(index);
    m_tabs->tabBar()->setStyleSheet(QStringLiteral(
      "QTabBar::tab { background: #e8edf2; border: 1px solid #bcc5cf;"
      " border-bottom: none; border-top-left-radius: 6px;"
      " border-top-right-radius: 6px; padding: 7px 17px; min-width: 76px;"
      " max-width: 76px; margin-right: 6px; color: #334454; }"
      "QTabBar::tab:hover { background: #f2f6fa; }"
      "QTabBar::tab:selected { font-weight: 700; background: #ffffff;"
      " border-top: 3px solid %1; border-left: 2px solid %1;"
      " border-right: 2px solid %1; border-bottom: none;"
      " border-top-left-radius: 6px; border-top-right-radius: 6px;"
      " padding-top: 5px; padding-left: 16px; padding-right: 16px;"
      " color: #142b3d; }").arg(color));
  };
  connect(m_tabs, &QTabWidget::currentChanged, this,
          [applySelectedTabBorderColor](int index) {
            applySelectedTabBorderColor(index);
          });
  connect(m_tabs, &QTabWidget::currentChanged, this, [this](int index) {
    QTableView* table = nullptr;
    if (m_tabs->widget(index) == m_flashUi.page) {
      table = m_flashUi.table;
    } else if (m_tabs->widget(index) == m_eepromUi.page) {
      table = m_eepromUi.table;
    }
    if (!table) return;
    QTimer::singleShot(0, table, [table] {
      if (table->selectionModel()
          && table->selectionModel()->selectedIndexes().isEmpty()) {
        table->setCurrentIndex(QModelIndex());
      }
    });
  });
  applySelectedTabBorderColor(m_tabs->currentIndex());

  mainLayout->addWidget(m_tabs, 1);

  auto* bottom = new QHBoxLayout;
  m_progress = new QProgressBar(central);
  m_progress->setRange(0, 100);
  m_progress->setValue(0);
  m_progress->setTextVisible(true);
  m_clearLogButton = new QPushButton(QStringLiteral("Clear"), central);
  m_cancelButton = new QPushButton(QStringLiteral("Cancel"), central);
  m_cancelButton->setEnabled(false);
  bottom->addWidget(m_progress, 1);
  bottom->addWidget(m_clearLogButton);
  bottom->addWidget(m_cancelButton);
  mainLayout->addLayout(bottom);
  connect(m_tabs, &QTabWidget::currentChanged, this,
          [this, fullLogTabIndex](int index) {
    if (m_clearLogButton) m_clearLogButton->setVisible(index != fullLogTabIndex);
  });

  setCentralWidget(central);
  statusBar()->showMessage(DisplayLanguage::text(QStringLiteral("Ready")));

  connect(m_deviceCombo, qOverload<int>(&QComboBox::currentIndexChanged),
          this, &MainWindow::deviceChanged);
  connect(m_sckDial, &QDial::valueChanged, this, [this](int) {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("sckClockId"), currentClockId());
    settings.setValue(QStringLiteral("sckClockSchema"), 2);
  });
  connect(m_detectButton, &QToolButton::clicked, this, [this] {
    if (m_busy) return;
    m_signatureCheckOnly = false;
    startProgress(ProgressState::Idle);
    setBusy(true, QStringLiteral("Detecting Target..."), false);
    Q_EMIT requestDetect(currentClockId());
  });
  connect(m_checkMcuButton, &QToolButton::clicked, this, [this] {
    if (m_busy) return;
    m_signatureCheckOnly = true;
    startProgress(ProgressState::Idle);
    setBusy(true, QStringLiteral("Checking Selected MCU..."), false);
    Q_EMIT requestDetect(currentClockId());
  });
  connect(m_flashMeter, &QAbstractButton::clicked, this, [this] {
    readMemory(true);
  });
  connect(m_eepromMeter, &QAbstractButton::clicked, this, [this] {
    readMemory(false);
  });
  connect(m_flashWriteButton, &QToolButton::clicked, this, [this] {
    const bool verify = m_verifyAfterWriteCheck && m_verifyAfterWriteCheck->isChecked();
    writeMemory(true, true, verify, confirmMemoryWrite());
  });
  connect(m_eepromWriteButton, &QToolButton::clicked, this, [this] {
    const bool verify = m_verifyAfterWriteCheck && m_verifyAfterWriteCheck->isChecked();
    writeMemory(false, false, verify, confirmMemoryWrite());
  });
  connect(m_flashLoadButton, &QPushButton::clicked, this, [this] {
    loadImage(true);
  });
  connect(m_flashSaveButton, &QPushButton::clicked, this, [this] {
    saveImage(true);
  });
  connect(m_eepromLoadButton, &QPushButton::clicked, this, [this] {
    loadImage(false);
  });
  connect(m_eepromSaveButton, &QPushButton::clicked, this, [this] {
    saveImage(false);
  });
  connect(m_projectLoadButton, &QPushButton::clicked,
          this, &MainWindow::loadProject);
  connect(m_projectSaveButton, &QPushButton::clicked,
          this, &MainWindow::saveProject);
  connect(m_startTaskButton, &QPushButton::clicked,
          this, &MainWindow::startTaskSequence);
  connect(m_preFuseButton, &QPushButton::clicked, this, [this] {
    openFuseDialog(true);
  });
  connect(m_finalFuseButton, &QPushButton::clicked, this, [this] {
    openFuseDialog(false);
  });
  connect(m_lockFuseButton, &QPushButton::clicked,
          this, &MainWindow::openLockDialog);
  connect(m_clearLogButton, &QPushButton::clicked, this, [this] {
    if (m_miniLog) m_miniLog->clear();
    statusBar()->showMessage(DisplayLanguage::text(QStringLiteral("Log cleared")), 1800);
  });
  connect(m_cancelButton, &QPushButton::clicked, this, [this] {
    if (m_controller) {
      m_controller->requestCancel();
      appendLog(QStringLiteral("Cancellation requested; the current USB transfer must finish first."));
      if (!m_taskSequenceActive) {
        setMiniLog(QStringLiteral("Cancellation requested"), false);
      }
    }
  });

  auto* undoShortcut = new QShortcut(QKeySequence(QKeySequence::Undo), this);
  auto* redoShortcut = new QShortcut(QKeySequence(QKeySequence::Redo), this);
  connect(undoShortcut, &QShortcut::activated, this, [this] {
    HexTableModel* model = m_tabs->currentWidget() == m_eepromUi.page
      ? m_eepromUi.model : m_flashUi.model;
    if (model) model->undoStack()->undo();
  });
  connect(redoShortcut, &QShortcut::activated, this, [this] {
    HexTableModel* model = m_tabs->currentWidget() == m_eepromUi.page
      ? m_eepromUi.model : m_flashUi.model;
    if (model) model->undoStack()->redo();
  });

  m_operationWidgets = {
    m_deviceCombo, m_detectButton, m_checkMcuButton, m_sckDial,
    m_flashMeter, m_eepromMeter,
    m_flashWriteButton, m_eepromWriteButton,
    m_flashLoadButton, m_flashSaveButton,
    m_eepromLoadButton, m_eepromSaveButton,
    m_projectLoadButton, m_projectSaveButton,
    m_startTaskButton, m_preFuseButton, m_finalFuseButton, m_lockFuseButton
  };

  setStyleSheet(QStringLiteral(
    "QMainWindow { background: #f3f5f7; }"
    "QGroupBox { font-weight: 600; border: 1px solid #c9ced6; border-radius: 5px; margin-top: 10px; padding-top: 5px; background: #ffffff; }"
    "QGroupBox::title { font-size: 16px; font-weight: 700; subcontrol-origin: margin; left: 8px; padding: 0 4px; color: #263747; }"
    "QPushButton, QToolButton { border: 1px solid #aeb6c1; border-radius: 4px; padding: 4px 8px; background: #ffffff; }"
    "QPushButton:hover, QToolButton:hover { background: #eaf2fb; border-color: #6d9fd0; }"
    "QPushButton:pressed, QToolButton:pressed { background: #dce9f6; }"
    "QPushButton:disabled, QToolButton:disabled { color: #9098a3; background: #eef0f2; }"
    "QTabWidget::pane { border: 1px solid #bcc5cf; border-radius: 4px; background: #ffffff; top: -1px; }"
    "QTabWidget::tab-bar { left: 16px; }"
    "QTabBar::tab { background: #e8edf2; border: 1px solid #bcc5cf; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; padding: 7px 17px; min-width: 76px; max-width: 76px; margin-right: 6px; color: #334454; }"
    "QTabBar::tab:hover { background: #f2f6fa; }"
    "QTabBar::tab:selected { font-weight: 700; background: #ffffff; border-top: 3px solid #2b7db7; border-left: 2px solid #2b7db7; border-right: 2px solid #2b7db7; border-bottom: none; border-top-left-radius: 6px; border-top-right-radius: 6px; padding-top: 5px; padding-left: 16px; padding-right: 16px; color: #142b3d; }"
    "QLineEdit, QComboBox, QPlainTextEdit, QTableView { background: #ffffff; border: 1px solid #b8c0ca; }"
    "QComboBox { min-height: 31px; padding: 1px 28px 1px 7px; font-size: 12px; }"
    "QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 25px; border-left: 1px solid #c9ced6; }"
    "QComboBox::down-arrow { image: url(:/icons/chevron_down.png); width: 12px; height: 8px; }"
    "QCheckBox { spacing: 7px; }"
    "QProgressBar { border: 1px solid #aab2bd; border-radius: 3px; text-align: center; background: #ffffff; }"
    "QProgressBar::chunk { background: #ffffff; }"));
  setProgressState(ProgressState::Idle);
}

QWidget* MainWindow::buildTopStrip(QWidget* parent) {
  auto* strip = new QWidget(parent);
  strip->setFixedHeight(110);
  auto* layout = new QHBoxLayout(strip);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);

  auto* mcuBox = new QGroupBox(QStringLiteral("MCU"), strip);
  mcuBox->setFixedHeight(110);
  mcuBox->setMinimumWidth(260);
  auto* mcuLayout = new QVBoxLayout(mcuBox);
  mcuLayout->setContentsMargins(6, 8, 6, 4);
  mcuLayout->setSpacing(3);
  m_deviceCombo = new QComboBox(mcuBox);
  m_deviceCombo->setMinimumWidth(245);
  m_deviceCombo->setIconSize(QSize(18, 18));
  m_deviceCombo->setMaxVisibleItems(24);
  m_deviceCombo->setToolTip(QStringLiteral("175 AVR SPI ISP/TPI device definitions"));
  m_signatureLabel = new QLabel(
    QStringLiteral("<b>ID:</b> [-- -- --]<br>"
                   "<span style='color:#287bb8; font-weight:700;'>Flash: --</span>"
                   "&nbsp;&nbsp;&nbsp;"
                   "<span style='color:#2d9b57; font-weight:700;'>EEPROM: --</span>"),
    mcuBox);
  m_signatureLabel->setTextFormat(Qt::RichText);
  m_signatureLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_signatureLabel->setMinimumHeight(38);
  m_signatureLabel->setStyleSheet(QStringLiteral("color: #27313d;"));
  m_checkMcuButton = makeImageButton(QString(),
    QStringLiteral(":/icons/MCU_Search.png"), mcuBox);
  m_checkMcuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_checkMcuButton->setFixedSize(42, 42);
  m_checkMcuButton->setIconSize(QSize(39, 40));
  m_checkMcuButton->setToolTip(
    QStringLiteral("Read the MCU signature and check it against the selected MCU."));
  m_checkMcuButton->setAccessibleName(QStringLiteral("Check selected MCU signature"));
  auto* mcuDetailsLayout = new QHBoxLayout;
  mcuDetailsLayout->setContentsMargins(0, 0, 0, 0);
  mcuDetailsLayout->setSpacing(3);
  mcuDetailsLayout->addWidget(m_signatureLabel, 1);
  mcuDetailsLayout->addWidget(m_checkMcuButton, 0, Qt::AlignRight | Qt::AlignVCenter);
  mcuLayout->addWidget(m_deviceCombo);
  mcuLayout->addLayout(mcuDetailsLayout);
  mcuLayout->addStretch();
  layout->addWidget(mcuBox);

  auto* programmerBox = new QGroupBox(QStringLiteral("Programmer"), strip);
  programmerBox->setFixedHeight(110);
  programmerBox->setFixedWidth(190);
  auto* programmerLayout = new QHBoxLayout(programmerBox);
  programmerLayout->setContentsMargins(5, 8, 5, 4);
  programmerLayout->setSpacing(7);
  m_usbaspButton = new QToolButton(programmerBox);
  m_usbaspButton->setText(QString());
  m_usbaspButton->setIcon(QIcon(QStringLiteral(":/icons/usbasp_off.png")));
  m_usbaspButton->setIconSize(QSize(76, 76));
  m_usbaspButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_usbaspButton->setFixedSize(85, 85);
  m_usbaspButton->setFocusPolicy(Qt::NoFocus);
  m_usbaspButton->setCursor(Qt::ArrowCursor);
  m_usbaspButton->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  removeImageButtonOutline(m_usbaspButton);
  m_usbaspButton->setToolTip(QStringLiteral("USBasp status: disconnected. Detection is automatic."));
  m_detectButton = makeImageButton(QString(),
    QStringLiteral(":/icons/MCU_Search_B.png"), programmerBox);
  m_detectButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  m_detectButton->setIconSize(QSize(76, 78));
  m_detectButton->setToolTip(QStringLiteral("Read the AVR signature and automatically select a matching MCU."));
  m_detectButton->setAccessibleName(QStringLiteral("Search for MCU"));
  programmerLayout->addWidget(m_usbaspButton);
  programmerLayout->addWidget(m_detectButton);
  layout->addWidget(programmerBox);

  auto* clockBox = new QGroupBox(QStringLiteral("SCK"), strip);
  clockBox->setFixedSize(130, 110);
  auto* clockLayout = new QVBoxLayout(clockBox);
  clockLayout->setContentsMargins(1, 1, 1, 4);
  m_sckDial = new SckDial(clockBox);
  m_sckDial->setFixedSize(124, 92);
  m_sckDial->setClockId(static_cast<int>(usbasp::IspClock::Pro));
  clockLayout->addWidget(m_sckDial, 0, Qt::AlignCenter);
  layout->addWidget(clockBox);

  auto* memoryBox = new QGroupBox(QStringLiteral("Flash and EEPROM Memories"), strip);
  memoryBox->setFixedHeight(110);
  memoryBox->setMinimumWidth(300);
  auto* memoryLayout = new QGridLayout(memoryBox);
  memoryLayout->setContentsMargins(6, 5, 6, 7);
  memoryLayout->setHorizontalSpacing(5);
  memoryLayout->setVerticalSpacing(4);

  m_flashMeter = new MemoryMeterButton(QStringLiteral("Flash"), memoryBox);
  m_eepromMeter = new MemoryMeterButton(QStringLiteral("EEPROM"), memoryBox);
  m_flashMeter->setMinimumWidth(100);
  m_flashMeter->setMaximumWidth(500);
  m_flashMeter->setFixedHeight(40);
  m_eepromMeter->setMinimumWidth(100);
  m_eepromMeter->setMaximumWidth(500);
  m_eepromMeter->setFixedHeight(40);
  m_flashIconOn = QPixmap(QStringLiteral(":/icons/Mem-Flash_B.png"));
  m_flashIconOff = QPixmap(QStringLiteral(":/icons/Mem-Flash_B_off.png"));
  m_flashIconLight = QPixmap(QStringLiteral(":/icons/Mem-Flash_B_light_only.png"));
  m_eepromIconOn = QPixmap(QStringLiteral(":/icons/Mem-EEPROM_B.png"));
  m_eepromIconOff = QPixmap(QStringLiteral(":/icons/Mem-EEPROM_B_off.png"));
  m_eepromIconLight = QPixmap(QStringLiteral(":/icons/Mem-EEPROM_B_light_only.png"));
  m_flashWriteButton = makeImageButton(QString(),
    QStringLiteral(":/icons/Mem-Flash_B.png"), memoryBox);
  m_eepromWriteButton = makeImageButton(QString(),
    QStringLiteral(":/icons/Mem-EEPROM_B.png"), memoryBox);
  for (QToolButton* button : {m_flashWriteButton, m_eepromWriteButton}) {
    button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    button->setFixedSize(86, 86);
    button->setIconSize(QSize(78, 82));
  }
  m_flashWriteButton->setToolTip(QStringLiteral("Write Flash to MCU"));
  m_flashWriteButton->setAccessibleName(QStringLiteral("Write Flash to MCU"));
  m_eepromWriteButton->setToolTip(QStringLiteral("Write EEPROM to MCU"));
  m_eepromWriteButton->setAccessibleName(QStringLiteral("Write EEPROM to MCU"));
  m_memoryActivityTimer = new QTimer(this);
  m_memoryActivityTimer->setInterval(40);
  connect(m_memoryActivityTimer, &QTimer::timeout,
          this, &MainWindow::updateMemoryActivityFrame);
  memoryLayout->addWidget(m_flashWriteButton, 0, 0, 2, 1, Qt::AlignCenter);
  memoryLayout->addWidget(m_flashMeter, 0, 1);
  memoryLayout->addWidget(m_eepromMeter, 1, 1);
  memoryLayout->addWidget(m_eepromWriteButton, 0, 2, 2, 1, Qt::AlignCenter);
  memoryLayout->setColumnStretch(1, 1);
  layout->addWidget(memoryBox, 1);

  auto* fileBox = new QGroupBox(QStringLiteral("Files"), strip);
  fileBox->setFixedHeight(110);
  fileBox->setMinimumWidth(315);
  fileBox->setMaximumWidth(350);
  auto* fileLayout = new QGridLayout(fileBox);
  fileLayout->setContentsMargins(6, 8, 6, 4);
  fileLayout->setHorizontalSpacing(4);
  fileLayout->setVerticalSpacing(3);
  m_flashLoadButton = makeButton(QStringLiteral("Load"),
    QStringLiteral(":/icons/load.png"), fileBox);
  m_flashSaveButton = makeButton(QStringLiteral("Save"),
    QStringLiteral(":/icons/save.png"), fileBox);
  m_eepromLoadButton = makeButton(QStringLiteral("Load"),
    QStringLiteral(":/icons/load_eeprom.png"), fileBox);
  m_eepromSaveButton = makeButton(QStringLiteral("Save"),
    QStringLiteral(":/icons/save_eeprom.png"), fileBox);
  m_projectLoadButton = makeButton(QStringLiteral("Load Project"),
    QStringLiteral(":/icons/project_load_purple.png"), fileBox);
  m_projectSaveButton = makeButton(QStringLiteral("Save Project"),
    QStringLiteral(":/icons/project_save_pink.png"), fileBox);
  for (QPushButton* button : {m_flashLoadButton, m_flashSaveButton,
                              m_eepromLoadButton, m_eepromSaveButton,
                              m_projectLoadButton, m_projectSaveButton}) {
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(27);
    button->setMaximumHeight(29);
  }
  auto* flashFileLabel = new QLabel(QStringLiteral("Flash"), fileBox);
  auto* eepromFileLabel = new QLabel(QStringLiteral("EEPROM"), fileBox);
  auto* projectFileLabel = new QLabel(QStringLiteral("Project"), fileBox);
  flashFileLabel->setStyleSheet(QStringLiteral(
    "QLabel { color: #1766b5; font-weight: 700; }"));
  eepromFileLabel->setStyleSheet(QStringLiteral(
    "QLabel { color: #228B22; font-weight: 700; }"));
  projectFileLabel->setStyleSheet(QStringLiteral(
    "QLabel { color: #B8860B; font-weight: 700; }"));
  m_flashLoadButton->setStyleSheet(QStringLiteral(
    "QPushButton { color: #1766b5; font-weight: 700; }"));
  m_flashSaveButton->setStyleSheet(QStringLiteral(
    "QPushButton { color: #1766b5; font-weight: 700; }"));
  const QString eepromFileButtonStyle = QStringLiteral(
    "QPushButton { color: #228B22; font-weight: 700; }"
    "QPushButton:hover { background: #eaf2fb; border-color: #6d9fd0; }"
    "QPushButton:pressed { background: #dce9f6; }");
  m_eepromLoadButton->setStyleSheet(eepromFileButtonStyle);
  m_eepromSaveButton->setStyleSheet(eepromFileButtonStyle);
  m_projectLoadButton->setStyleSheet(QStringLiteral(
    "QPushButton { color: #8a5cc2; font-weight: 700; }"));
  m_projectSaveButton->setStyleSheet(QStringLiteral(
    "QPushButton { color: #FFA07A; font-weight: 700; }"));
  fileLayout->addWidget(flashFileLabel, 0, 0);
  fileLayout->addWidget(m_flashLoadButton, 0, 1);
  fileLayout->addWidget(m_flashSaveButton, 0, 2);
  fileLayout->addWidget(eepromFileLabel, 1, 0);
  fileLayout->addWidget(m_eepromLoadButton, 1, 1);
  fileLayout->addWidget(m_eepromSaveButton, 1, 2);
  fileLayout->addWidget(projectFileLabel, 2, 0);
  fileLayout->addWidget(m_projectLoadButton, 2, 1);
  fileLayout->addWidget(m_projectSaveButton, 2, 2);
  fileLayout->setColumnStretch(1, 1);
  fileLayout->setColumnStretch(2, 1);
  layout->addWidget(fileBox, 0, Qt::AlignRight);

  return strip;
}

QWidget* MainWindow::createTaskPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(8, 7, 8, 7);
  pageLayout->setSpacing(5);

  auto* taskBox = new QGroupBox(QStringLiteral("Automatic Programming Sequence"), page);
  taskBox->setMaximumHeight(235);
  auto* taskLayout = new QVBoxLayout(taskBox);
  taskLayout->setContentsMargins(12, 11, 12, 8);
  taskLayout->setSpacing(5);

  const QStringList names{
    QStringLiteral("Confirm Signature"),
    QStringLiteral("Erase Chip"),
    QStringLiteral("Pre-Write Fuses"),
    QStringLiteral("Program Flash"),
    QStringLiteral("Program EEPROM"),
    QStringLiteral("Verify Flash"),
    QStringLiteral("Verify EEPROM"),
    QStringLiteral("Write Final Fuses"),
    QStringLiteral("Lock Fuse")
  };
  for (const QString& name : names) {
    auto* check = new QCheckBox(name, taskBox);
    check->setFocusPolicy(Qt::NoFocus);
    m_taskChecks.append(check);
  }
  for (QCheckBox* check : m_taskChecks) {
    connect(check, &QCheckBox::toggled, this, [this] {
      saveTaskSelections();
    });
  }

  auto* hiddenFuseStorage = new QWidget(page);
  hiddenFuseStorage->hide();
  const QRegularExpression hexByte(QStringLiteral("^[0-9A-Fa-f]{2}$"));
  for (int i = 0; i < 3; ++i) {
    auto* preEdit = new QLineEdit(hiddenFuseStorage);
    auto* finalEdit = new QLineEdit(hiddenFuseStorage);
    preEdit->setValidator(new QRegularExpressionValidator(hexByte, preEdit));
    finalEdit->setValidator(new QRegularExpressionValidator(hexByte, finalEdit));
    m_preFuseEdits.append(preEdit);
    m_fuseEdits.append(finalEdit);
  }
  m_lockEdit = new QLineEdit(hiddenFuseStorage);
  m_lockEdit->setValidator(new QRegularExpressionValidator(hexByte, m_lockEdit));

  m_preFuseButton = new QPushButton(QStringLiteral("0xFFFF"), taskBox);
  m_finalFuseButton = new QPushButton(QStringLiteral("0xFFFF"), taskBox);
  m_lockFuseButton = new QPushButton(QStringLiteral("0xFF"), taskBox);
  for (QPushButton* button : {m_preFuseButton, m_finalFuseButton, m_lockFuseButton}) {
    button->setFocusPolicy(Qt::NoFocus);
    button->setMinimumHeight(25);
    button->setMaximumHeight(27);
    button->setStyleSheet(QStringLiteral(
      "QPushButton { font-family: Consolas; font-weight: 700; padding: 2px 7px; }"));
  }
  auto makeTaskRow = [taskBox](QCheckBox* check, QPushButton* valueButton = nullptr,
                               const QString& chipIconClass = {},
                               const QString& resourceIcon = {}) {
    auto* row = new QWidget(taskBox);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);
    layout->addWidget(check);
    if (!resourceIcon.isEmpty()) {
      auto* taskIcon = new QLabel(row);
      const QIcon icon(resourceIcon);
      taskIcon->setPixmap(icon.pixmap(18, 18));
      taskIcon->setFixedSize(18, 18);
      layout->addWidget(taskIcon);
    }
    layout->addStretch();
    if (!chipIconClass.isEmpty()) {
      auto* taskIcon = new QLabel(row);
      const QIcon icon = mcuClassIcon(chipIconClass);
      taskIcon->setPixmap(icon.pixmap(18, 18));
      taskIcon->setFixedSize(18, 18);
      layout->addWidget(taskIcon);
    }
    if (valueButton) layout->addWidget(valueButton);
    row->setFixedHeight(layout->sizeHint().height());
    return row;
  };

  auto* leftColumn = new QWidget(taskBox);
  auto* leftLayout = new QVBoxLayout(leftColumn);
  leftLayout->setContentsMargins(0, 0, 0, 0);
  leftLayout->setSpacing(5);
  leftLayout->setAlignment(Qt::AlignTop);
  leftLayout->addWidget(makeTaskRow(m_taskChecks[0]));
  leftLayout->addWidget(makeTaskRow(m_taskChecks[1]));
  leftLayout->addWidget(makeTaskRow(m_taskChecks[2], m_preFuseButton));
  leftLayout->addWidget(makeTaskRow(
    m_taskChecks[3], nullptr, QStringLiteral("ATmega")));
  leftLayout->addWidget(makeTaskRow(
    m_taskChecks[4], nullptr, QStringLiteral("ATtiny")));

  auto* rightColumn = new QWidget(taskBox);
  auto* rightLayout = new QVBoxLayout(rightColumn);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  rightLayout->setSpacing(5);
  rightLayout->addWidget(makeTaskRow(
    m_taskChecks[5], nullptr, QStringLiteral("ATmega")));
  rightLayout->addWidget(makeTaskRow(
    m_taskChecks[6], nullptr, QStringLiteral("ATtiny")));
  rightLayout->addWidget(makeTaskRow(m_taskChecks[7], m_finalFuseButton));
  rightLayout->addWidget(makeTaskRow(
    m_taskChecks[8], m_lockFuseButton, {},
    QStringLiteral(":/icons/lock_gold.png")));

  rightColumn->setMinimumWidth(188);
  m_startTaskButton = new QPushButton(QIcon(QStringLiteral(":/icons/Button_Start.png")),
                                      QString(), taskBox);
  m_startTaskButton->setFocusPolicy(Qt::NoFocus);
  m_startTaskButton->setCursor(Qt::PointingHandCursor);
  m_startTaskButton->setFixedSize(170, 46);
  m_startTaskButton->setIconSize(QSize(164, 39));
  m_startTaskButton->setStyleSheet(QStringLiteral(
    "QPushButton { border: none; border-radius: 8px; background: transparent; padding: 0px; }"
    "QPushButton:hover { border: none; background: rgba(43, 125, 183, 24); }"
    "QPushButton:pressed { border: none; background: rgba(43, 125, 183, 48); padding-left: 2px; padding-top: 2px; }"
    "QPushButton:disabled { border: none; background: transparent; }"));
  m_startTaskButton->setAccessibleName(QStringLiteral("Start automatic programming sequence"));
  rightLayout->addWidget(m_startTaskButton, 0, Qt::AlignHCenter);

  // Keep both task columns on the same container bounds. Task rows on
  // both sides use the same explicit 5 px vertical spacing; the left
  // column no longer stretches its inter-row gaps to fill the height.
  const int alignedTaskColumnHeight = rightLayout->sizeHint().height();
  leftColumn->setFixedHeight(alignedTaskColumnHeight);
  rightColumn->setFixedHeight(alignedTaskColumnHeight);

  auto* columns = new QHBoxLayout;
  columns->setContentsMargins(0, 0, 0, 0);
  columns->addStretch(1);
  columns->addWidget(leftColumn, 0, Qt::AlignTop);
  columns->addSpacing(125);
  columns->addWidget(rightColumn, 0, Qt::AlignTop);
  columns->addStretch(1);
  taskLayout->addLayout(columns);
  pageLayout->addWidget(taskBox);

  auto* miniBox = new QGroupBox(QStringLiteral("Log"), page);
  auto* miniLayout = new QVBoxLayout(miniBox);
  miniLayout->setContentsMargins(5, 10, 5, 5);
  m_miniLog = new QPlainTextEdit(miniBox);
  m_miniLog->setReadOnly(true);
  m_miniLog->setMaximumBlockCount(10000);
  m_miniLog->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_miniLog->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_miniLog->setStyleSheet(QStringLiteral(
    "QPlainTextEdit { background: #f8fbfa; color: #000000; border: 1px solid #9fc7b7; padding: 4px 7px; }"));
  miniLayout->addWidget(m_miniLog, 1);
  pageLayout->addWidget(miniBox, 1);
  return page;
}

MainWindow::MemoryUi MainWindow::createMemoryPage(const QString& title,
                                                   bool flash,
                                                   QWidget* parent) {
  MemoryUi ui;
  ui.page = new QWidget(parent);
  auto* layout = new QVBoxLayout(ui.page);
  layout->setContentsMargins(5, 5, 5, 5);
  layout->setSpacing(4);

  ui.model = new HexTableModel(flash, ui.page);
  ui.table = new QTableView(ui.page);
  configureHexTable(ui.table, ui.model);

  auto* memoryBlock = new QWidget(ui.page);
  memoryBlock->setFixedWidth(ui.table->width());
  auto* memoryLayout = new QVBoxLayout(memoryBlock);
  memoryLayout->setContentsMargins(0, 0, 0, 0);
  memoryLayout->setSpacing(4);
  memoryLayout->addWidget(ui.table, 1);

  auto* editRow = new QHBoxLayout;
  ui.clearAllButton = new QPushButton(QStringLiteral("Clear all"), memoryBlock);
  ui.clearSelectedButton = new QPushButton(
    QStringLiteral("Clear Selected"), memoryBlock);
  ui.zeroAllButton = new QPushButton(QStringLiteral("Zero all"), memoryBlock);
  ui.zeroSelectedButton = new QPushButton(
    QStringLiteral("Zero Selected"), memoryBlock);
  ui.undoButton = new QPushButton(QStringLiteral("Undo"), memoryBlock);
  ui.redoButton = new QPushButton(QStringLiteral("Redo"), memoryBlock);
  const QString clearButtonStyle = QStringLiteral(
    "QPushButton { color: #CD5C5C; font-weight: 700; }"
    "QPushButton:hover { background: #fbecec; border-color: #CD5C5C; }"
    "QPushButton:pressed { background: #f3dddd; }");
  const QString zeroButtonStyle = QStringLiteral(
    "QPushButton { color: #1766B5; font-weight: 700; }"
    "QPushButton:hover { background: #eaf2fb; border-color: #6d9fd0; }"
    "QPushButton:pressed { background: #dce9f6; }");
  ui.clearAllButton->setStyleSheet(clearButtonStyle);
  ui.clearSelectedButton->setStyleSheet(clearButtonStyle);
  ui.zeroAllButton->setStyleSheet(zeroButtonStyle);
  ui.zeroSelectedButton->setStyleSheet(zeroButtonStyle);
  if (flash) {
    ui.bootInfoLabel = new QLabel(memoryBlock);
    ui.bootInfoLabel->setTextFormat(Qt::RichText);
    ui.bootInfoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui.bootInfoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui.bootInfoLabel->setMinimumWidth(0);
    ui.bootInfoLabel->setText(QStringLiteral("<b>Bootloader:</b> n/a"));
    ui.bootInfoLabel->setToolTip(QStringLiteral(
      "Supported bootloader sizes and AVR word start addresses. "
      "When BOOTRST is enabled, only the HEX and DEC address cells in the active boot section are shaded light blue."));
    editRow->addWidget(ui.bootInfoLabel, 1, Qt::AlignVCenter);
    // Keep the boot text close to the editing controls while reserving exactly
    // 1.5 memory-byte-cell widths before the Clear all button.
    editRow->addSpacing(51);
  } else {
    editRow->addStretch(1);
  }
  editRow->addWidget(ui.clearAllButton);
  editRow->addWidget(ui.clearSelectedButton);
  editRow->addWidget(ui.zeroAllButton);
  editRow->addWidget(ui.zeroSelectedButton);
  editRow->addWidget(ui.undoButton);
  editRow->addWidget(ui.redoButton);
  memoryLayout->addLayout(editRow);

  auto* centeredRow = new QHBoxLayout;
  centeredRow->setContentsMargins(0, 0, 0, 0);
  centeredRow->addStretch(1);
  centeredRow->addWidget(memoryBlock, 0);
  centeredRow->addStretch(1);
  layout->addLayout(centeredRow, 1);

  HexTableModel* model = ui.model;
  QTableView* table = ui.table;
  QPushButton* clearSelectedButton = ui.clearSelectedButton;
  QPushButton* zeroSelectedButton = ui.zeroSelectedButton;
  connect(ui.clearAllButton, &QPushButton::clicked, this,
          [this, flash, model] {
    model->clearBuffer();
    setMiniLog(QStringLiteral("%1 buffer cleared").arg(memoryName(flash)), true);
  });
  connect(ui.clearSelectedButton, &QPushButton::clicked, this,
          [this, flash, model, table] {
    const QList<qsizetype> offsets = selectedMemoryOffsets(
      table, model->image().capacity());
    if (offsets.isEmpty()) return;
    model->clearOffsets(offsets);
    setMiniLog(QStringLiteral("%1 selected bytes cleared")
      .arg(memoryName(flash)), true);
  });
  connect(ui.zeroAllButton, &QPushButton::clicked, this,
          [this, flash, model] {
    model->fill(0xFF, true);
    setMiniLog(QStringLiteral("%1 buffer zeroed to defined 0xFF")
      .arg(memoryName(flash)), true);
  });
  connect(ui.zeroSelectedButton, &QPushButton::clicked, this,
          [this, flash, model, table] {
    const QList<qsizetype> offsets = selectedMemoryOffsets(
      table, model->image().capacity());
    if (offsets.isEmpty()) return;
    model->fillOffsets(offsets, 0xFF, true);
    setMiniLog(QStringLiteral("%1 selected bytes zeroed to defined 0xFF")
      .arg(memoryName(flash)), true);
  });
  connect(ui.undoButton, &QPushButton::clicked,
          model->undoStack(), &QUndoStack::undo);
  connect(ui.redoButton, &QPushButton::clicked,
          model->undoStack(), &QUndoStack::redo);
  connect(model, &HexTableModel::imageChanged,
          this, &MainWindow::updateMemoryIndicators);
  connect(model, &HexTableModel::modifiedChanged, this,
          [this, title](bool modified) {
    statusBar()->showMessage(DisplayLanguage::text(modified
      ? QStringLiteral("%1 buffer modified").arg(title)
      : QStringLiteral("Ready")), 2200);
  });
  connect(model->undoStack(), &QUndoStack::canUndoChanged,
          ui.undoButton, &QWidget::setEnabled);
  connect(model->undoStack(), &QUndoStack::canRedoChanged,
          ui.redoButton, &QWidget::setEnabled);
  connect(table->selectionModel(), &QItemSelectionModel::selectionChanged,
          this, [table, model, clearSelectedButton, zeroSelectedButton] {
    const bool hasSelection = !selectedMemoryOffsets(
      table, model->image().capacity()).isEmpty();
    clearSelectedButton->setEnabled(hasSelection);
    zeroSelectedButton->setEnabled(hasSelection);
  });
  ui.clearSelectedButton->setEnabled(false);
  ui.zeroSelectedButton->setEnabled(false);
  ui.undoButton->setEnabled(false);
  ui.redoButton->setEnabled(false);
  return ui;
}

QWidget* MainWindow::createFullLogPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(5, 5, 5, 5);
  auto* row = new QHBoxLayout;
  auto* saveButton = new QPushButton(QStringLiteral("Save Log"), page);
  auto* copyButton = new QPushButton(QStringLiteral("Copy All"), page);
  auto* clearButton = new QPushButton(QStringLiteral("Clear"), page);
  row->addWidget(saveButton);
  row->addWidget(copyButton);
  row->addWidget(clearButton);
  row->addStretch();
  row->addWidget(new QLabel(QStringLiteral(
    "Detailed USB, ISP, address, retry, and operation history"), page));
  layout->addLayout(row);

  m_log = new QPlainTextEdit(page);
  m_log->setReadOnly(true);
  m_log->setMaximumBlockCount(50000);
  m_log->setLineWrapMode(QPlainTextEdit::NoWrap);
  m_log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  m_log->setStyleSheet(QStringLiteral("QPlainTextEdit { color: #000000; }"));
  layout->addWidget(m_log, 1);

  connect(saveButton, &QPushButton::clicked, this, [this] {
    const QString suggested = QDir(lastFileDirectory()).filePath(
      QStringLiteral("FlyingBytesPro.log.txt"));
    QString path = QFileDialog::getSaveFileName(
      this, DisplayLanguage::text(QStringLiteral("Save Full Log")), suggested,
      DisplayLanguage::text(QStringLiteral("Text Log (*.txt);;All Files (*.*)")));
    if (path.isEmpty()) return;
    rememberFileDirectory(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(m_log->toPlainText().toUtf8()) < 0) {
      showResult(false, QStringLiteral("Log save failed: %1").arg(file.errorString()));
      return;
    }
    setMiniLog(QStringLiteral("Full log saved to %1").arg(path), true);
  });
  connect(copyButton, &QPushButton::clicked, this, [this] {
    QApplication::clipboard()->setText(m_log->toPlainText());
    setMiniLog(QStringLiteral("Full log copied to clipboard"), true);
  });
  connect(clearButton, &QPushButton::clicked, m_log, &QPlainTextEdit::clear);
  return page;
}

QWidget* MainWindow::createSettingsPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* layout = new QVBoxLayout(page);
  layout->setContentsMargins(22, 18, 22, 18);
  layout->setSpacing(10);

  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));

  auto* languageBox = new QGroupBox(QStringLiteral("Language"), page);
  auto* languageLayout = new QHBoxLayout(languageBox);
  languageLayout->setContentsMargins(12, 12, 12, 12);
  languageLayout->addWidget(new QLabel(QStringLiteral("Display Language:"), languageBox));
  m_languageCombo = new QComboBox(languageBox);
  m_languageCombo->setProperty("displayLanguageSelector", true);
  for (const DisplayLanguage::LanguageInfo& language : DisplayLanguage::availableLanguages()) {
    m_languageCombo->addItem(language.nativeName, language.code);
  }
  int languageIndex = m_languageCombo->findData(DisplayLanguage::currentCode());
  if (languageIndex < 0) languageIndex = 0;
  m_languageCombo->setCurrentIndex(languageIndex);
  languageLayout->addWidget(m_languageCombo);
  languageLayout->addStretch();

  auto* writeBox = new QGroupBox(QStringLiteral("Write Options"), page);
  auto* writeLayout = new QVBoxLayout(writeBox);
  writeLayout->setContentsMargins(12, 12, 12, 12);
  m_confirmMemoryWritesCheck = new QCheckBox(
    QStringLiteral("Confirm Before Writing Flash or EEPROM"), writeBox);
  m_confirmMemoryWritesCheck->setChecked(
    settings.value(QStringLiteral("confirmMemoryWrites"), false).toBool());
  writeLayout->addWidget(m_confirmMemoryWritesCheck);
  m_verifyAfterWriteCheck = new QCheckBox(
    QStringLiteral("Verify Flash or EEPROM After Writing"), writeBox);
  m_verifyAfterWriteCheck->setChecked(
    settings.value(QStringLiteral("verifyAfterWrite"), false).toBool());
  writeLayout->addWidget(m_verifyAfterWriteCheck);
  layout->addWidget(writeBox);

  auto* readBox = new QGroupBox(QStringLiteral("Flash Read"), page);
  auto* readLayout = new QHBoxLayout(readBox);
  readLayout->setContentsMargins(12, 12, 12, 12);
  readLayout->addWidget(new QLabel(QStringLiteral("Read Mode:"), readBox));
  m_flashReadModeCombo = new QComboBox(readBox);
  m_flashReadModeCombo->addItem(QStringLiteral("Smart Read"), QStringLiteral("smart"));
  m_flashReadModeCombo->addItem(QStringLiteral("Full MCU Read"), QStringLiteral("full"));
  const QString savedReadMode = settings.value(
    QStringLiteral("flashReadMode"), QStringLiteral("smart")).toString();
  int readModeIndex = m_flashReadModeCombo->findData(savedReadMode);
  if (readModeIndex < 0) readModeIndex = 0;
  m_flashReadModeCombo->setCurrentIndex(readModeIndex);
  m_flashReadModeCombo->setToolTip(QStringLiteral(
    "Smart Read stops at the first fully erased Flash page. Full MCU Read scans the complete Flash address space."));
  readLayout->addWidget(m_flashReadModeCombo);
  readLayout->addStretch();
  layout->addWidget(readBox);

  auto* signatureBox = new QGroupBox(QStringLiteral("MCU Signature Check"), page);
  auto* signatureLayout = new QVBoxLayout(signatureBox);
  signatureLayout->setContentsMargins(12, 12, 12, 12);
  m_ignoreMcuSignatureMatchingCheck = new QCheckBox(
    QStringLiteral("Use Selected MCU Without Checking Its Signature"), signatureBox);
  m_ignoreMcuSignatureMatchingCheck->setChecked(
    settings.value(QStringLiteral("ignoreMcuSignatureMatching"), false).toBool());
  signatureLayout->addWidget(m_ignoreMcuSignatureMatchingCheck);
  layout->addWidget(signatureBox);
  layout->addWidget(languageBox);

  auto* driverBox = new QGroupBox(QStringLiteral("Install Driver"), page);
  auto* driverLayout = new QHBoxLayout(driverBox);
  driverLayout->setContentsMargins(12, 12, 12, 12);
  auto* driverButton = new QPushButton(QStringLiteral("USBasp"), driverBox);
  driverLayout->addWidget(driverButton);
  driverLayout->addStretch();
  layout->addWidget(driverBox);

  auto* restoreBox = new QGroupBox(QStringLiteral("Restore Defaults"), page);
  auto* restoreLayout = new QHBoxLayout(restoreBox);
  restoreLayout->setContentsMargins(12, 12, 12, 12);
  auto* restoreDefaultsButton = new QPushButton(
    QStringLiteral("Restore Default Settings"), restoreBox);
  restoreLayout->addWidget(restoreDefaultsButton);
  restoreLayout->addStretch();
  layout->addWidget(restoreBox);
  layout->addStretch();

  connect(m_languageCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (!m_languageCombo || index < 0) return;
    const QString nextCode = m_languageCombo->itemData(index).toString();
    const QString previousCode = DisplayLanguage::currentCode();
    if (nextCode == previousCode) return;
    DisplayLanguage::setCurrentCode(nextCode);
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("displayLanguage"), nextCode);
    settings.sync();
    applyDisplayLanguage(previousCode);
    setMiniLog(QStringLiteral("Language: %1")
      .arg(m_languageCombo->currentText()), true);
  });

  connect(m_confirmMemoryWritesCheck, &QCheckBox::toggled, this, [](bool enabled) {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("confirmMemoryWrites"), enabled);
  });
  connect(m_verifyAfterWriteCheck, &QCheckBox::toggled, this, [](bool enabled) {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("verifyAfterWrite"), enabled);
  });
  connect(m_flashReadModeCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("flashReadMode"),
                      m_flashReadModeCombo->currentData().toString());
  });
  connect(m_ignoreMcuSignatureMatchingCheck, &QCheckBox::toggled,
          this, [](bool enabled) {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("ignoreMcuSignatureMatching"), enabled);
    settings.sync();
  });


  connect(driverButton, &QPushButton::clicked, this, [this]() {
    UsbAspDriverDialog dialog(this);
    dialog.exec();
  });
  connect(restoreDefaultsButton, &QPushButton::clicked, this, [this]() {
    const QString previousCode = DisplayLanguage::currentCode();
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.remove(QStringLiteral("displayLanguage"));
    settings.remove(QStringLiteral("confirmMemoryWrites"));
    settings.remove(QStringLiteral("verifyAfterWrite"));
    settings.remove(QStringLiteral("flashReadMode"));
    settings.remove(QStringLiteral("ignoreMcuSignatureMatching"));
    settings.sync();

    {
      const QSignalBlocker languageBlocker(m_languageCombo);
      const QSignalBlocker confirmBlocker(m_confirmMemoryWritesCheck);
      const QSignalBlocker verifyBlocker(m_verifyAfterWriteCheck);
      const QSignalBlocker readModeBlocker(m_flashReadModeCombo);
      const QSignalBlocker signatureBlocker(m_ignoreMcuSignatureMatchingCheck);

      const int englishIndex = m_languageCombo
        ? m_languageCombo->findData(QStringLiteral("en")) : -1;
      if (m_languageCombo && englishIndex >= 0) {
        m_languageCombo->setCurrentIndex(englishIndex);
      }
      if (m_confirmMemoryWritesCheck) m_confirmMemoryWritesCheck->setChecked(false);
      if (m_verifyAfterWriteCheck) m_verifyAfterWriteCheck->setChecked(false);
      if (m_ignoreMcuSignatureMatchingCheck) {
        m_ignoreMcuSignatureMatchingCheck->setChecked(false);
      }
      if (m_flashReadModeCombo) {
        const int smartIndex = m_flashReadModeCombo->findData(QStringLiteral("smart"));
        m_flashReadModeCombo->setCurrentIndex(smartIndex >= 0 ? smartIndex : 0);
      }
    }

    DisplayLanguage::setCurrentCode(QStringLiteral("en"));
    applyDisplayLanguage(previousCode);
    setMiniLog(QStringLiteral("Settings restored to defaults."), true);
  });
  return page;
}

QWidget* MainWindow::createAboutPage(QWidget* parent) {
  auto* page = new QWidget(parent);
  auto* pageLayout = new QVBoxLayout(page);
  pageLayout->setContentsMargins(24, 18, 24, 18);

  auto* contentLayout = new QHBoxLayout;
  contentLayout->setContentsMargins(0, 0, 0, 0);
  contentLayout->setSpacing(100);
  contentLayout->addStretch(1);

  auto* donationSection = new QWidget(page);
  donationSection->setFixedWidth(300);
  auto* donationLayout = new QVBoxLayout(donationSection);
  donationLayout->setContentsMargins(0, 0, 0, 0);
  donationLayout->addStretch(1);

  auto* donationButton = new QToolButton(donationSection);
  donationButton->setText(QStringLiteral("Buy Me a Coffee"));
  donationButton->setIcon(QIcon(QStringLiteral(":/icons/Pigeon_Coffee.png")));
  donationButton->setIconSize(QSize(240, 234));
  donationButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  donationButton->setFixedSize(275, 285);
  donationButton->setFocusPolicy(Qt::NoFocus);
  donationButton->setCursor(Qt::PointingHandCursor);
  donationButton->setToolTip(QStringLiteral("Support FlyingBytesPro"));
  donationButton->setStyleSheet(QStringLiteral(
    "QToolButton { border: none; border-radius: 12px; background: transparent; "
    "font-size: 15px; font-weight: 700; color: #8a4f08; padding: 4px; }"
    "QToolButton:hover { background: rgba(184, 134, 11, 22); }"
    "QToolButton:pressed { background: rgba(184, 134, 11, 42); "
    "padding-left: 6px; padding-top: 6px; padding-right: 2px; padding-bottom: 2px; }"));
  connect(donationButton, &QToolButton::clicked, this, [this] {
    const bool opened = QDesktopServices::openUrl(QUrl(QStringLiteral(
      "https://paypal.me/flyandance?country.x=US&locale.x=en_US")));
    if (!opened) {
      showResult(false, QStringLiteral("The donation page could not be opened."));
    }
  });
  donationLayout->addWidget(donationButton, 0, Qt::AlignCenter);
  donationLayout->addStretch(1);

  auto* aboutSection = new QWidget(page);
  aboutSection->setMaximumWidth(570);
  auto* aboutLayout = new QVBoxLayout(aboutSection);
  aboutLayout->setContentsMargins(0, 0, 0, 0);
  aboutLayout->setSpacing(8);

  auto* aboutTop = new QWidget(aboutSection);
  auto* aboutTopLayout = new QHBoxLayout(aboutTop);
  aboutTopLayout->setContentsMargins(0, 0, 0, 0);
  aboutTopLayout->setSpacing(18);

  auto* titleBlock = new QWidget(aboutTop);
  auto* titleLayout = new QVBoxLayout(titleBlock);
  titleLayout->setContentsMargins(0, 0, 0, 0);
  titleLayout->setSpacing(5);
  titleLayout->addStretch(1);

  auto* title = new QLabel(QStringLiteral("FlyingBytesPro V3.2.29"), titleBlock);
  title->setAlignment(Qt::AlignCenter);
  title->setStyleSheet(QStringLiteral(
    "font-size: 25px; font-weight: 700; color: #174f8a;"));
  titleLayout->addWidget(title);

  auto* author = new QLabel(
    QStringLiteral("by Flyandance JZ from San Francisco, 2026"), titleBlock);
  author->setAlignment(Qt::AlignCenter);
  titleLayout->addWidget(author);
  titleLayout->addStretch(1);

  auto* icon = new QToolButton(aboutTop);
  icon->setIcon(QIcon(QStringLiteral(":/icons/FD-Logo.png")));
  icon->setIconSize(QSize(112, 112));
  icon->setFixedSize(120, 120);
  icon->setFocusPolicy(Qt::NoFocus);
  icon->setCursor(Qt::PointingHandCursor);
  icon->setToolTip(QStringLiteral("Open FlyingBytesPro on GitHub"));
  icon->setStyleSheet(QStringLiteral(
    "QToolButton { border: none; background: transparent; padding: 4px; }"
    "QToolButton:hover { background: rgba(23, 79, 138, 18); border-radius: 10px; }"
    "QToolButton:pressed { background: rgba(23, 79, 138, 38); border-radius: 10px; "
    "padding-left: 6px; padding-top: 6px; padding-right: 2px; padding-bottom: 2px; }"));
  connect(icon, &QToolButton::clicked, this, [this] {
    const bool opened = QDesktopServices::openUrl(QUrl(QStringLiteral(
      "https://github.com/flyandancexo/FlyingBytesPro")));
    if (!opened) {
      showResult(false, QStringLiteral("The FlyingBytesPro GitHub page could not be opened."));
    }
  });

  aboutTopLayout->addWidget(titleBlock, 1);
  aboutTopLayout->addWidget(icon, 0, Qt::AlignRight | Qt::AlignVCenter);
  aboutLayout->addWidget(aboutTop);

  m_aboutFeaturesLabel = new QLabel(aboutFeaturesText(), aboutSection);
  m_aboutFeaturesLabel->setTextFormat(Qt::RichText);
  m_aboutFeaturesLabel->setWordWrap(true);
  m_aboutFeaturesLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  m_aboutFeaturesLabel->setMaximumWidth(570);
  aboutLayout->addWidget(m_aboutFeaturesLabel);
  aboutLayout->addStretch(1);

  contentLayout->addWidget(donationSection, 0, Qt::AlignVCenter);
  contentLayout->addWidget(aboutSection, 0, Qt::AlignVCenter);
  contentLayout->addStretch(1);
  pageLayout->addLayout(contentLayout, 1);
  return page;
}

void MainWindow::connectController() {
  connect(this, &MainWindow::requestProbe,
          m_controller, &ProgrammerController::probe, Qt::QueuedConnection);
  connect(this, &MainWindow::requestDetect,
          m_controller, &ProgrammerController::detectTarget, Qt::QueuedConnection);
  connect(this, &MainWindow::requestReadFlash,
          m_controller, &ProgrammerController::readFlash, Qt::QueuedConnection);
  connect(this, &MainWindow::requestReadEeprom,
          m_controller, &ProgrammerController::readEeprom, Qt::QueuedConnection);
  connect(this, &MainWindow::requestWriteFlash,
          m_controller, &ProgrammerController::writeFlash, Qt::QueuedConnection);
  connect(this, &MainWindow::requestWriteEeprom,
          m_controller, &ProgrammerController::writeEeprom, Qt::QueuedConnection);
  connect(this, &MainWindow::requestVerifyFlash,
          m_controller, &ProgrammerController::verifyFlash, Qt::QueuedConnection);
  connect(this, &MainWindow::requestVerifyEeprom,
          m_controller, &ProgrammerController::verifyEeprom, Qt::QueuedConnection);
  connect(this, &MainWindow::requestErase,
          m_controller, &ProgrammerController::eraseChip, Qt::QueuedConnection);
  connect(this, &MainWindow::requestBlankFlash,
          m_controller, &ProgrammerController::blankCheckFlash, Qt::QueuedConnection);
  connect(this, &MainWindow::requestBlankEeprom,
          m_controller, &ProgrammerController::blankCheckEeprom, Qt::QueuedConnection);
  connect(this, &MainWindow::requestReadFuses,
          m_controller, &ProgrammerController::readFuses, Qt::QueuedConnection);
  connect(this, &MainWindow::requestWriteFuses,
          m_controller, &ProgrammerController::writeFuses, Qt::QueuedConnection);
  connect(this, &MainWindow::requestWriteLock,
          m_controller, &ProgrammerController::writeLock, Qt::QueuedConnection);

  connect(m_controller, &ProgrammerController::logMessage,
          this, &MainWindow::appendLog);
  connect(m_controller, &ProgrammerController::progressChanged,
          this, &MainWindow::setProgress);
  connect(m_controller, &ProgrammerController::probeFinished,
          this, &MainWindow::handleProbeFinished);
  connect(m_controller, &ProgrammerController::signatureFinished,
          this, &MainWindow::handleSignatureFinished);
  connect(m_controller, &ProgrammerController::imageFinished,
          this, &MainWindow::handleImageFinished);
  connect(m_controller, &ProgrammerController::operationFinished,
          this, &MainWindow::handleOperationFinished);
  connect(m_controller, &ProgrammerController::fusesFinished,
          this, &MainWindow::handleFusesFinished);
}

void MainWindow::configureHexTable(QTableView* table, HexTableModel* model) {
  table->setModel(model);
  table->setAlternatingRowColors(true);
  table->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table->setSelectionBehavior(QAbstractItemView::SelectItems);
  // Do not let a memory page taking focus manufacture a current top-left cell.
  // The table still receives focus normally when the user clicks it.
  table->setFocusPolicy(Qt::ClickFocus);
  table->setCurrentIndex(QModelIndex());
  table->setEditTriggers(QAbstractItemView::DoubleClicked
                         | QAbstractItemView::EditKeyPressed
                         | QAbstractItemView::SelectedClicked);
  table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  table->setWordWrap(false);
  table->setCornerButtonEnabled(false);
  table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  table->verticalHeader()->setVisible(false);
  table->verticalHeader()->setDefaultSectionSize(23);
  table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
  table->horizontalHeader()->resizeSection(0, 96);
  for (int column = 1; column <= 16; ++column) {
    table->horizontalHeader()->resizeSection(column, 34);
  }
  table->horizontalHeader()->resizeSection(17, 96);
  table->horizontalHeader()->resizeSection(18, 160);

  // Keep the fixed memory columns unchanged and size the table to exactly
  // those columns plus its vertical scrollbar and frame. This removes the
  // unused strip between the ASCII column and the scrollbar without
  // stretching any data column.
  const int memoryTableWidth = table->horizontalHeader()->length()
    + table->verticalScrollBar()->sizeHint().width()
    + (2 * table->frameWidth());
  table->setFixedWidth(memoryTableWidth);
}

void MainWindow::populateDevices() {
  m_deviceCombo->blockSignals(true);
  m_deviceCombo->clear();
  for (const AvrDevice& device : m_database.devices()) {
    m_deviceCombo->addItem(mcuClassIcon(device.name), device.name, device.id);
  }
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  const QString savedDeviceId = settings.value(QStringLiteral("selectedDeviceId")).toString();
  int preferred = savedDeviceId.isEmpty() ? -1 : m_deviceCombo->findData(savedDeviceId);
  if (preferred < 0) {
    preferred = m_deviceCombo->findData(QStringLiteral("atmega88"));
  }
  if (preferred < 0) {
    preferred = m_deviceCombo->findData(QStringLiteral("atmega328p"));
  }
  m_deviceCombo->setCurrentIndex(preferred >= 0 ? preferred : 0);
  m_deviceCombo->blockSignals(false);
  deviceChanged(m_deviceCombo->currentIndex());
}

const AvrDevice* MainWindow::currentDevice() const {
  return m_database.byId(m_deviceCombo->currentData().toString());
}

int MainWindow::currentClockId() const {
  return m_sckDial ? m_sckDial->clockId() : static_cast<int>(usbasp::IspClock::Pro);
}

void MainWindow::deviceChanged(int index) {
  if (m_revertingDevice) return;
  if (m_previousDeviceIndex >= 0 && !m_deviceChangeIsDetection
      && !confirmDiscardModifiedBuffers()) {
    m_revertingDevice = true;
    m_deviceCombo->setCurrentIndex(m_previousDeviceIndex);
    m_revertingDevice = false;
    return;
  }

  if (m_previousDeviceIndex >= 0 && m_previousDeviceIndex < m_deviceCombo->count()) {
    saveFuseValuesForDeviceId(m_deviceCombo->itemData(m_previousDeviceIndex).toString());
  }

  const AvrDevice* device = currentDevice();
  if (!device) return;
  m_previousDeviceIndex = index;
  {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("selectedDeviceId"), device->id);
  }
  resetBuffersForDevice(*device);
  updateFuseUi(*device);
  loadDefaultFuseValues(*device);
  restoreFuseValuesForDevice(*device);
  m_signatureLabel->setText(deviceDetailsText(*device));
  appendLog(QStringLiteral("Selected %1: Flash %2 bytes/page %3; EEPROM %4 bytes/page %5.")
    .arg(device->name)
    .arg(device->flashSize)
    .arg(device->flashPageSize)
    .arg(device->eepromSize)
    .arg(device->eepromPageSize));
}

bool MainWindow::confirmDiscardModifiedBuffers() {
  if (!m_flashUi.model || !m_eepromUi.model) return true;
  if (!m_flashUi.model->isModified() && !m_eepromUi.model->isModified()) return true;
  return showLocalizedMessageBox(this, QMessageBox::Question,
    QStringLiteral("Discard Buffer Changes"),
    QStringLiteral("Changing the MCU resets the Flash and EEPROM buffers. Discard unsaved changes?"),
    QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
}

void MainWindow::resetBuffersForDevice(const AvrDevice& device) {
  m_flashUi.model->setImage(FirmwareImage(device.flashSize, 0xFF));
  m_eepromUi.model->setImage(FirmwareImage(device.eepromSize, 0xFF));
  const bool hasEeprom = device.eepromSize > 0;
  m_eepromUi.page->setEnabled(hasEeprom);
  if (m_eepromMeter) m_eepromMeter->setEnabled(hasEeprom);
  if (m_eepromWriteButton) m_eepromWriteButton->setEnabled(hasEeprom);
  if (m_eepromLoadButton) m_eepromLoadButton->setEnabled(hasEeprom);
  if (m_eepromSaveButton) m_eepromSaveButton->setEnabled(hasEeprom);
  if (m_taskChecks.size() > 6) {
    m_taskChecks[4]->setEnabled(hasEeprom);
    m_taskChecks[6]->setEnabled(hasEeprom);
  }
  updateMemoryIndicators();
}

void MainWindow::updateFuseUi(const AvrDevice& device) {
  const bool hasFuses = device.fuseCount() > 0;
  const bool hasWritableFuse = std::any_of(
    device.fuseProgramMasks.cbegin(), device.fuseProgramMasks.cend(),
    [](quint8 mask) { return mask != 0; });
  m_preFuseButton->setVisible(hasFuses);
  m_finalFuseButton->setVisible(hasFuses);
  m_preFuseButton->setEnabled(!m_busy && hasWritableFuse);
  m_finalFuseButton->setEnabled(!m_busy && hasWritableFuse);

  const bool hasLock = device.hasLockByte();
  m_lockFuseButton->setVisible(hasLock);
  m_lockFuseButton->setEnabled(!m_busy && hasLock && device.lockProgramMask != 0);

  updateFuseButtonTexts();
}

void MainWindow::loadDefaultFuseValues(const AvrDevice& device) {
  for (int i = 0; i < 3; ++i) {
    const int value = i < device.fuseFactoryValues.size()
      ? device.fuseFactoryValues.at(i) : 0xFF;
    const QString text = byteText(value);
    m_preFuseEdits[i]->setText(text);
    m_fuseEdits[i]->setText(text);
  }
  m_lockEdit->setText(byteText(device.lockFactoryValue));
  updateFuseButtonTexts();
}

void MainWindow::updateFuseButtonTexts() {
  const auto editValue = [](const QVector<QLineEdit*>& edits, int index, int fallback) {
    if (index < 0 || index >= edits.size()) return fallback;
    bool ok = false;
    const int value = edits.at(index)->text().toInt(&ok, 16);
    return ok ? value : fallback;
  };
  const int preLow = editValue(m_preFuseEdits, 0, 0xFF);
  const int preHigh = editValue(m_preFuseEdits, 1, 0xFF);
  const int finalLow = editValue(m_fuseEdits, 0, 0xFF);
  const int finalHigh = editValue(m_fuseEdits, 1, 0xFF);
  bool lockOk = false;
  const int lockValue = m_lockEdit ? m_lockEdit->text().toInt(&lockOk, 16) : 0xFF;
  if (m_preFuseButton) {
    m_preFuseButton->setText(QStringLiteral("0x%1%2")
      .arg(preHigh, 2, 16, QLatin1Char('0'))
      .arg(preLow, 2, 16, QLatin1Char('0')).toUpper());
  }
  if (m_finalFuseButton) {
    m_finalFuseButton->setText(QStringLiteral("0x%1%2")
      .arg(finalHigh, 2, 16, QLatin1Char('0'))
      .arg(finalLow, 2, 16, QLatin1Char('0')).toUpper());
  }
  if (m_lockFuseButton) {
    m_lockFuseButton->setText(QStringLiteral("0x%1")
      .arg(lockOk ? lockValue : 0xFF, 2, 16, QLatin1Char('0')).toUpper());
  }
  updateBootloaderUi();
}

void MainWindow::updateBootloaderUi() {
  if (!m_flashUi.model) return;
  const AvrDevice* device = currentDevice();
  if (!device) {
    m_flashUi.model->setBootloaderHighlight(false, 0);
    if (m_flashUi.bootInfoLabel) {
      m_flashUi.bootInfoLabel->setText(QStringLiteral("<b>Bootloader:</b> n/a"));
    }
    return;
  }

  const BootSectionMetadata metadata = bootSectionMetadataForDevice(*device);
  if (m_flashUi.bootInfoLabel) {
    m_flashUi.bootInfoLabel->setText(bootSectionSupportText(*device, metadata));
  }

  if (metadata.choices.isEmpty()
      || metadata.resetFuseByteIndex < 0
      || metadata.resetMask == 0
      || metadata.resetFuseByteIndex >= m_fuseEdits.size()) {
    m_flashUi.model->setBootloaderHighlight(false, 0);
    return;
  }

  bool resetOk = false;
  const int resetFuseValue = m_fuseEdits.at(metadata.resetFuseByteIndex)
    ->text().toInt(&resetOk, 16);
  if (!resetOk
      || (resetFuseValue & metadata.resetMask) != metadata.resetEnabledValue) {
    m_flashUi.model->setBootloaderHighlight(false, 0);
    return;
  }

  for (const BootSectionChoice& choice : metadata.choices) {
    if (choice.fuseByteIndex < 0
        || choice.fuseByteIndex >= m_fuseEdits.size()
        || choice.mask == 0) {
      continue;
    }
    bool sizeOk = false;
    const int fuseValue = m_fuseEdits.at(choice.fuseByteIndex)
      ->text().toInt(&sizeOk, 16);
    if (!sizeOk || (fuseValue & choice.mask) != choice.value) continue;

    const qsizetype startByte = static_cast<qsizetype>(choice.startWordAddress) * 2;
    m_flashUi.model->setBootloaderHighlight(true, startByte);
    return;
  }

  m_flashUi.model->setBootloaderHighlight(false, 0);
}

void MainWindow::openFuseDialog(bool prewrite) {
  openFuseAndLockDialog(prewrite, false);
}

void MainWindow::openLockDialog() {
  openFuseAndLockDialog(false, true);
}

void MainWindow::openFuseAndLockDialog(bool prewrite, bool focusLock) {
  const AvrDevice* device = currentDevice();
  if (!device || (device->fuseCount() == 0
      && !device->hasLockByte())) return;

  QVector<QLineEdit*>& storage = prewrite ? m_preFuseEdits : m_fuseEdits;
  QVector<int> values;
  for (int i = 0; i < device->fuseCount(); ++i) {
    bool ok = false;
    const int value = i < storage.size()
      ? storage.at(i)->text().toInt(&ok, 16) : 0xFF;
    values.append(ok ? value : device->fuseFactoryValues.value(i, 0xFF));
  }
  bool lockOk = false;
  const int lockValue = m_lockEdit
    ? m_lockEdit->text().toInt(&lockOk, 16) : device->lockFactoryValue;

  FuseLockDialog dialog(*device, values,
                        lockOk ? lockValue : device->lockFactoryValue,
                        fuseMetadataForDevice(*device), prewrite, this);
  m_activeFuseEditor = &dialog;

  auto applyDialogValues = [&] {
    const QVector<int> editedValues = dialog.fuseValues();
    for (int i = 0; i < editedValues.size() && i < storage.size(); ++i) {
      storage.at(i)->setText(byteText(editedValues.at(i)));
    }
    if (m_lockEdit && device->hasLockByte()) {
      m_lockEdit->setText(byteText(dialog.lockValue()));
    }
    updateFuseButtonTexts();
    saveFuseValuesForDeviceId(device->id);
  };

  connect(&dialog, &FuseLockDialog::readRequested, this, [this, device, &dialog] {
    if (m_busy) return;
    dialog.setStatus(QStringLiteral("Reading fuse and lock values..."), true);
    startProgress(ProgressState::Idle);
    setBusy(true, QStringLiteral("Reading Fuse and Lock Values..."), false);
    Q_EMIT requestReadFuses(*device, currentClockId());
  });
  connect(&dialog, &FuseLockDialog::writeFusesRequested,
          this, [this, device, &dialog, &applyDialogValues](const QVector<int>& valuesToWrite) {
    if (m_busy) return;
    applyDialogValues();
    dialog.setStatus(QStringLiteral("Writing fuse values..."), true);
    startProgress(ProgressState::Idle);
    setBusy(true, QStringLiteral("Writing Fuses..."), false);
    Q_EMIT requestWriteFuses(*device, valuesToWrite, currentClockId());
  });
  connect(&dialog, &FuseLockDialog::writeLockRequested,
          this, [this, device, &dialog, &applyDialogValues](int valueToWrite) {
    if (m_busy) return;
    applyDialogValues();
    if (showLocalizedMessageBox(&dialog, QMessageBox::Warning,
          QStringLiteral("Write Lock Fuse"),
          QStringLiteral("Write lock fuse value 0x%1 to %2?")
            .arg(valueToWrite & 0xFF, 2, 16, QLatin1Char('0'))
            .arg(device->name),
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) return;
    dialog.setStatus(QStringLiteral("Writing lock value..."), true);
    startProgress(ProgressState::Idle);
    setBusy(true, QStringLiteral("Writing Lock Fuse..."), false);
    Q_EMIT requestWriteLock(*device, valueToWrite, currentClockId());
  });

  if (focusLock) dialog.focusLockSection();
  dialog.exec();
  applyDialogValues();
  if (m_activeFuseEditor == &dialog) m_activeFuseEditor.clear();
}

void MainWindow::applyDisplayLanguage(const QString& sourceCode) {
  DisplayLanguage::translateWidgetTree(this, sourceCode);
  if (m_tabs && m_tabs->tabBar()) {
    const QStringList sourceTabNames{
      QStringLiteral("Task"), QStringLiteral("Flash"), QStringLiteral("EEPROM"),
      QStringLiteral("Full Log"), QStringLiteral("Settings"), QStringLiteral("About")
    };
    QFont tabFont = m_tabs->tabBar()->font();
    tabFont.setBold(true);
    const QFontMetrics metrics(tabFont);
    constexpr int fixedTabContentWidth = 76;
    constexpr int iconTextGap = 4;
    for (int i = 0; i < sourceTabNames.size() && i < m_tabs->count(); ++i) {
      const QString& english = sourceTabNames.at(i);
      const QString translated = DisplayLanguage::text(english);
      int availableTextWidth = fixedTabContentWidth;
      if (!m_tabs->tabIcon(i).isNull()) {
        int iconWidth = m_tabs->tabBar()->iconSize().width();
        if (iconWidth <= 0) {
          iconWidth = m_tabs->style()->pixelMetric(QStyle::PM_SmallIconSize);
        }
        availableTextWidth -= iconWidth + iconTextGap;
      }
      m_tabs->setTabText(i, metrics.horizontalAdvance(translated)
        > availableTextWidth ? english : translated);
    }
  }
  if (m_aboutFeaturesLabel) m_aboutFeaturesLabel->setText(aboutFeaturesText());
  if (m_flashUi.table) {
    m_flashUi.table->horizontalHeader()->viewport()->update();
    m_flashUi.table->viewport()->update();
  }
  if (m_eepromUi.table) {
    m_eepromUi.table->horizontalHeader()->viewport()->update();
    m_eepromUi.table->viewport()->update();
  }
  const AvrDevice* device = currentDevice();
  if (device && m_signatureLabel) {
    m_signatureLabel->setText(deviceDetailsText(*device));
  }
  updateMemoryIndicators();
  if (statusBar() && !statusBar()->currentMessage().isEmpty()) {
    statusBar()->showMessage(
      DisplayLanguage::textFrom(statusBar()->currentMessage(), sourceCode));
  }
}

void MainWindow::updateMemoryIndicators() {
  if (m_flashMeter && m_flashUi.model) {
    m_flashMeter->setImage(m_flashUi.model->image());
  }
  if (m_eepromMeter && m_eepromUi.model) {
    m_eepromMeter->setImage(m_eepromUi.model->image());
  }
}

QPixmap MainWindow::memoryActivityFrame(bool flash, qreal lightOpacity) const {
  const QPixmap& base = flash ? m_flashIconOff : m_eepromIconOff;
  const QPixmap& light = flash ? m_flashIconLight : m_eepromIconLight;
  if (base.isNull()) return flash ? m_flashIconOn : m_eepromIconOn;

  QPixmap frame(base);
  if (!light.isNull() && lightOpacity > 0.0) {
    QPainter painter(&frame);
    painter.setOpacity(std::clamp(lightOpacity, qreal(0.0), qreal(1.0)));
    painter.drawPixmap(0, 0, light);
  }
  return frame;
}

void MainWindow::startMemoryActivity(bool flash) {
  if (!m_flashWriteButton || !m_eepromWriteButton || !m_memoryActivityTimer) return;
  m_memoryActivity = flash ? MemoryActivity::Flash : MemoryActivity::Eeprom;
  m_memoryActivityElapsed.restart();
  updateMemoryActivityFrame();
  m_memoryActivityTimer->start();
}

void MainWindow::stopMemoryActivity() {
  if (m_memoryActivityTimer) m_memoryActivityTimer->stop();
  m_memoryActivityElapsed.invalidate();
  m_memoryActivity = MemoryActivity::None;
  if (m_flashWriteButton && !m_flashIconOn.isNull()) {
    m_flashWriteButton->setIcon(QIcon(m_flashIconOn));
  }
  if (m_eepromWriteButton && !m_eepromIconOn.isNull()) {
    m_eepromWriteButton->setIcon(QIcon(m_eepromIconOn));
  }
}

void MainWindow::updateMemoryActivityFrame() {
  if (m_memoryActivity == MemoryActivity::None || !m_memoryActivityElapsed.isValid()) return;

  constexpr double periodMs = 1000.0;
  constexpr double pi = 3.14159265358979323846;
  const double phase = std::fmod(static_cast<double>(m_memoryActivityElapsed.elapsed()), periodMs)
    / periodMs;
  const qreal breath = static_cast<qreal>(0.20 + 0.80 * 0.5
    * (1.0 - std::cos(2.0 * pi * phase)));

  if (m_memoryActivity == MemoryActivity::Flash && m_flashWriteButton) {
    m_flashWriteButton->setIcon(QIcon(memoryActivityFrame(true, breath)));
  } else if (m_memoryActivity == MemoryActivity::Eeprom && m_eepromWriteButton) {
    m_eepromWriteButton->setIcon(QIcon(memoryActivityFrame(false, breath)));
  }
}

void MainWindow::updateUsbStatus(bool connected, const QString& text) {
  m_usbConnected = connected;
  m_usbaspButton->setIcon(QIcon(connected
    ? QStringLiteral(":/icons/usbasp_on.png")
    : QStringLiteral(":/icons/usbasp_off.png")));
  const QString state = DisplayLanguage::text(text.isEmpty()
    ? (connected ? QStringLiteral("Connected") : QStringLiteral("Disconnected"))
    : text);
  m_usbaspButton->setToolTip(DisplayLanguage::text(
    QStringLiteral("USBasp status: %1. Detection is automatic.")).arg(state));
  m_usbaspButton->setAccessibleDescription(state);
}

void MainWindow::setBusy(bool busy, const QString& status,
                         bool disableOperationWidgets) {
  Q_UNUSED(disableOperationWidgets)
  m_busy = busy;
  for (QWidget* widget : m_operationWidgets) {
    if (widget) {
      widget->setAttribute(Qt::WA_TransparentForMouseEvents, busy);
    }
  }
  m_cancelButton->setEnabled(busy);
  if (!status.isEmpty()) {
    statusBar()->showMessage(DisplayLanguage::text(status));
  } else if (!busy) {
    statusBar()->showMessage(DisplayLanguage::text(QStringLiteral("Ready")));
  }
  if (!busy) {
    const AvrDevice* device = currentDevice();
    if (device) updateFuseUi(*device);
  }
}

void MainWindow::startProgress(ProgressState state) {
  if (!m_progress) return;
  m_progress->setValue(0);
  setProgressState(state);
}

void MainWindow::setProgressState(ProgressState state) {
  if (!m_progress) return;
  m_progressState = state;
  QString chunkColor;
  switch (state) {
  case ProgressState::Flash:
    chunkColor = QStringLiteral("#7fc8f2");
    break;
  case ProgressState::Eeprom:
    chunkColor = QStringLiteral("#8dd8ad");
    break;
  case ProgressState::Error:
    chunkColor = QStringLiteral("#CD5C5C");
    break;
  case ProgressState::Idle:
    chunkColor = QStringLiteral("#ffffff");
    break;
  }
  m_progress->setStyleSheet(QStringLiteral(
    "QProgressBar { border: 1px solid #aab2bd; border-radius: 3px; "
    "text-align: center; background: #ffffff; }"
    "QProgressBar::chunk { background: %1; }").arg(chunkColor));
}

void MainWindow::completeProgressResult(bool success) {
  if (!m_progress) return;
  if (!success) {
    m_progress->setValue(100);
    setProgressState(ProgressState::Error);
    return;
  }
  if (m_progressState == ProgressState::Flash
      || m_progressState == ProgressState::Eeprom) {
    m_progress->setValue(100);
    return;
  }
  m_progress->setValue(0);
  setProgressState(ProgressState::Idle);
}

void MainWindow::showResult(bool success, const QString& message) {
  completeProgressResult(success);
  const QString displayMessage = DisplayLanguage::text(message);
  appendLog(QStringLiteral("%1 %2")
    .arg(success ? QStringLiteral("A-Okay ☆☆☆")
                 : QStringLiteral("Error! ★★★"),
         displayMessage));
  setMiniLog(displayMessage, success);
}

void MainWindow::setMiniLog(const QString& message, bool success) {
  if (!m_miniLog) return;
  QString line = DisplayLanguage::text(message);
  line.replace(QLatin1Char('\n'), QLatin1Char(' '));
  line.replace(QLatin1Char('\r'), QLatin1Char(' '));
  const bool programmerStatus = line == DisplayLanguage::text(QStringLiteral("Programmer Online :)"))
    || line == DisplayLanguage::text(QStringLiteral("Programmer Offline :("));
  if (programmerStatus) {
    line.remove(QStringLiteral(" :)"));
    line.remove(QStringLiteral(" :("));
  }
  const QString marker = success
    ? QStringLiteral("A-Okay ☆☆☆")
    : QStringLiteral("Error! ★★★");
  line = QStringLiteral("[%1] %2 %3")
    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")),
         marker,
         line);
  QTextCursor cursor = m_miniLog->textCursor();
  cursor.movePosition(QTextCursor::End);
  if (!m_miniLog->toPlainText().isEmpty()) cursor.insertBlock();
  QTextCharFormat format;
  if (!success) {
    format.setForeground(QColor(QStringLiteral("#CD5C5C")));
  } else {
    format.setForeground(QColor(QStringLiteral("#000000")));
  }
  cursor.insertText(line, format);
  m_miniLog->setTextCursor(cursor);
  m_miniLog->ensureCursorVisible();
}

void MainWindow::setTaskMiniLogLine(const QString& message, bool success) {
  if (!m_miniLog) return;
  if (m_taskMiniLogTimestamp.isEmpty()) {
    m_taskMiniLogTimestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  }

  QString cleanMessage = DisplayLanguage::text(message);
  cleanMessage.replace(QLatin1Char('\n'), QLatin1Char(' '));
  cleanMessage.replace(QLatin1Char('\r'), QLatin1Char(' '));
  QString line = QStringLiteral("[%1] %2")
    .arg(m_taskMiniLogTimestamp,
         success ? QStringLiteral("A-Okay ☆☆☆")
                 : QStringLiteral("Error! ★★★"));
  if (!cleanMessage.isEmpty()) {
    line += QLatin1Char(' ');
    line += cleanMessage;
  }

  QTextDocument* document = m_miniLog->document();
  QTextBlock block = m_taskMiniLogBlockNumber >= 0
    ? document->findBlockByNumber(m_taskMiniLogBlockNumber)
    : QTextBlock();
  QTextCursor cursor(document);
  if (block.isValid()) {
    cursor.setPosition(block.position());
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
  } else {
    cursor.movePosition(QTextCursor::End);
    if (!m_miniLog->toPlainText().isEmpty()) cursor.insertBlock();
    m_taskMiniLogBlockNumber = cursor.block().blockNumber();
  }

  QTextCharFormat format;
  format.setForeground(QColor(success
    ? QStringLiteral("#000000")
    : QStringLiteral("#CD5C5C")));
  cursor.insertText(line, format);
  m_miniLog->setTextCursor(cursor);
  m_miniLog->ensureCursorVisible();
}

void MainWindow::restoreTaskSelections() {
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  QString saved = settings.value(QStringLiteral("taskSelections")).toString();
  if (saved.size() == 10 && m_taskChecks.size() == 9) {
    saved.remove(3, 1);
  }
  if (saved.size() == m_taskChecks.size()) {
    for (int i = 0; i < m_taskChecks.size(); ++i) {
      const QSignalBlocker blocker(m_taskChecks[i]);
      m_taskChecks[i]->setChecked(saved.at(i) == QLatin1Char('1'));
    }
  }
  saveTaskSelections();
}

void MainWindow::saveTaskSelections() const {
  if (m_taskChecks.isEmpty()) return;
  QString saved;
  saved.reserve(m_taskChecks.size());
  for (const QCheckBox* check : m_taskChecks) {
    saved.append(check && check->isChecked() ? QLatin1Char('1') : QLatin1Char('0'));
  }
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.setValue(QStringLiteral("taskSelections"), saved);
}

void MainWindow::restoreFuseValuesForDevice(const AvrDevice& device) {
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.beginGroup(QStringLiteral("fuseValues/%1").arg(device.id));
  const QStringList preValues = settings.value(QStringLiteral("pre")).toStringList();
  const QStringList finalValues = settings.value(QStringLiteral("final")).toStringList();
  const QString lockValue = normalizedHexByte(settings.value(QStringLiteral("lock")).toString());
  settings.endGroup();

  for (int i = 0; i < 3; ++i) {
    if (i < preValues.size()) {
      const QString value = normalizedHexByte(preValues.at(i));
      if (!value.isEmpty()) m_preFuseEdits[i]->setText(value);
    }
    if (i < finalValues.size()) {
      const QString value = normalizedHexByte(finalValues.at(i));
      if (!value.isEmpty()) m_fuseEdits[i]->setText(value);
    }
  }
  if (!lockValue.isEmpty()) m_lockEdit->setText(lockValue);
  updateFuseButtonTexts();
}

void MainWindow::saveFuseValuesForDeviceId(const QString& deviceId) const {
  if (deviceId.isEmpty() || m_preFuseEdits.size() < 3
      || m_fuseEdits.size() < 3 || !m_lockEdit) return;

  QStringList preValues;
  QStringList finalValues;
  for (int i = 0; i < 3; ++i) {
    preValues.append(normalizedHexByte(m_preFuseEdits[i]->text()));
    finalValues.append(normalizedHexByte(m_fuseEdits[i]->text()));
  }
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.beginGroup(QStringLiteral("fuseValues/%1").arg(deviceId));
  settings.setValue(QStringLiteral("pre"), preValues);
  settings.setValue(QStringLiteral("final"), finalValues);
  settings.setValue(QStringLiteral("lock"), normalizedHexByte(m_lockEdit->text()));
  settings.endGroup();
}

void MainWindow::loadImage(bool flash) {
  startProgress(ProgressState::Idle);
  const AvrDevice* device = currentDevice();
  if (!device) return;
  const QString path = QFileDialog::getOpenFileName(
    this, DisplayLanguage::text(QStringLiteral("Load %1 Buffer")).arg(memoryName(flash)),
    lastFileDirectory(),
    DisplayLanguage::text(QStringLiteral(
      "Firmware Images (*.hex *.ihx *.ihex *.bin);;Intel HEX (*.hex *.ihx *.ihex);;Raw binary (*.bin);;All Files (*.*)")));
  if (path.isEmpty()) return;
  rememberFileDirectory(path);

  const qsizetype capacity = flash ? device->flashSize : device->eepromSize;
  FirmwareImage image(capacity, 0xFF);
  QString error;
  bool ok = false;
  if (isHexPath(path)) {
    ok = IntelHex::loadFile(path, image, error);
  } else {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
      error = file.errorString();
    } else {
      image = FirmwareImage::fromBinary(file.readAll(), capacity, error, 0xFF);
      ok = error.isEmpty();
    }
  }
  if (!ok) {
    showResult(false, QStringLiteral("%1 load failed: %2")
      .arg(memoryName(flash), error));
    return;
  }

  MemoryUi& ui = flash ? m_flashUi : m_eepromUi;
  ui.model->setImage(image);
  ui.model->setClean();
  updateMemoryIndicators();
  appendLog(QStringLiteral("Loaded %1: %2 defined bytes into %3, CRC-16 0x%4.")
    .arg(path)
    .arg(image.definedCount())
    .arg(memoryName(flash))
    .arg(flash ? m_flashMeter->crc() : m_eepromMeter->crc(),
         4, 16, QLatin1Char('0')).toUpper());
  setMiniLog(QStringLiteral("%1 loaded: %2 bytes — CRC 0x%3")
    .arg(memoryName(flash))
    .arg(image.definedCount())
    .arg(flash ? m_flashMeter->crc() : m_eepromMeter->crc(),
         4, 16, QLatin1Char('0')).toUpper(), true);
}

void MainWindow::saveImage(bool flash) {
  startProgress(ProgressState::Idle);
  MemoryUi& ui = flash ? m_flashUi : m_eepromUi;
  const QString suggested = QDir(lastFileDirectory()).filePath(
    flash ? QStringLiteral("flash.hex") : QStringLiteral("eeprom.hex"));
  QString path = QFileDialog::getSaveFileName(
    this, DisplayLanguage::text(QStringLiteral("Save %1 Buffer")).arg(memoryName(flash)), suggested,
    DisplayLanguage::text(QStringLiteral(
      "Intel HEX (*.hex);;Raw binary (*.bin);;All Files (*.*)")));
  if (path.isEmpty()) return;
  if (!path.contains(QLatin1Char('.'))) path += QStringLiteral(".hex");
  rememberFileDirectory(path);

  QString error;
  bool ok = false;
  if (isHexPath(path)) {
    ok = IntelHex::saveFile(path, ui.model->image(), error);
  } else {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      error = file.errorString();
    } else {
      const QByteArray binary = ui.model->image().toBinary();
      ok = file.write(binary) == binary.size();
      if (!ok) error = file.errorString();
    }
  }
  if (!ok) {
    showResult(false, QStringLiteral("%1 save failed: %2")
      .arg(memoryName(flash), error));
    return;
  }
  ui.model->setClean();
  appendLog(QStringLiteral("Saved %1 buffer to %2.")
    .arg(memoryName(flash), path));
  setMiniLog(QStringLiteral("%1 buffer saved to %2")
    .arg(memoryName(flash), path), true);
}

void MainWindow::readMemory(bool flash) {
  if (m_busy) return;
  const AvrDevice* device = currentDevice();
  if (!device) return;
  MemoryUi& ui = flash ? m_flashUi : m_eepromUi;
  if (!flash && device->eepromSize == 0) {
    showResult(false, QStringLiteral("The selected MCU has no EEPROM."));
    return;
  }
  if (ui.model->isModified()
      && showLocalizedMessageBox(this, QMessageBox::Question,
           QStringLiteral("Replace Modified Buffer"),
           QStringLiteral("Reading the target will replace the modified %1 buffer. Continue?")
             .arg(memoryName(flash)),
           QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
    return;
  }
  startProgress(flash ? ProgressState::Flash : ProgressState::Eeprom);
  startMemoryActivity(flash);
  setBusy(true, QStringLiteral("Reading %1...").arg(memoryName(flash)));
  if (flash) Q_EMIT requestReadFlash(*device, currentClockId(), fullFlashRead());
  else Q_EMIT requestReadEeprom(*device, currentClockId());
}

void MainWindow::writeMemory(bool flash, bool eraseFirst,
                             bool verifyAfter, bool askConfirmation) {
  if (m_busy) return;
  const AvrDevice* device = currentDevice();
  if (!device) return;
  MemoryUi& ui = flash ? m_flashUi : m_eepromUi;
  if (ui.model->image().definedCount() == 0) {
    return;
  }
  FirmwareImage writeImage = ui.model->imageCopy();
  if (writeImage.capacity() == 0) {
    showResult(false, QStringLiteral("The selected MCU has no %1 memory.")
      .arg(memoryName(flash)));
    return;
  }
  if (askConfirmation) {
    const QString eraseText = eraseFirst
      ? QStringLiteral(" The target will be chip-erased first.") : QString();
    if (showLocalizedMessageBox(this, QMessageBox::Warning,
          QStringLiteral("Confirm Programming"),
          DisplayLanguage::text(QStringLiteral("Write %1 bytes to %2 %3?%4"))
            .arg(writeImage.definedCount())
            .arg(device->name, memoryName(flash), DisplayLanguage::text(eraseText)),
          QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel)
        != QMessageBox::Yes) {
      return;
    }
  }
  startProgress(flash ? ProgressState::Flash : ProgressState::Eeprom);
  startMemoryActivity(flash);
  setBusy(true, QStringLiteral("Writing %1...").arg(memoryName(flash)));
  if (flash) {
    Q_EMIT requestWriteFlash(*device, writeImage,
                             eraseFirst, verifyAfter, currentClockId());
  } else {
    Q_EMIT requestWriteEeprom(*device, writeImage,
                              verifyAfter, currentClockId());
  }
}

void MainWindow::verifyMemory(bool flash) {
  if (m_busy) return;
  const AvrDevice* device = currentDevice();
  if (!device) return;
  MemoryUi& ui = flash ? m_flashUi : m_eepromUi;
  if (ui.model->image().definedCount() == 0) {
    showResult(false, QStringLiteral("The %1 buffer has no defined bytes to verify.")
      .arg(memoryName(flash)));
    return;
  }
  startProgress(ProgressState::Idle);
  startMemoryActivity(flash);
  setBusy(true, QStringLiteral("Verifying %1...").arg(memoryName(flash)));
  if (flash) Q_EMIT requestVerifyFlash(*device, ui.model->imageCopy(), currentClockId());
  else Q_EMIT requestVerifyEeprom(*device, ui.model->imageCopy(), currentClockId());
}

void MainWindow::blankMemory(bool flash) {
  if (m_busy) return;
  const AvrDevice* device = currentDevice();
  if (!device) return;
  startProgress(ProgressState::Idle);
  startMemoryActivity(flash);
  setBusy(true, QStringLiteral("Blank-Checking %1...").arg(memoryName(flash)));
  if (flash) Q_EMIT requestBlankFlash(*device, currentClockId());
  else Q_EMIT requestBlankEeprom(*device, currentClockId());
}


void MainWindow::saveProject() {
  startProgress(ProgressState::Idle);
  const AvrDevice* device = currentDevice();
  if (!device) return;
  const QString suggested = QDir(lastFileDirectory()).filePath(
    QStringLiteral("%1.fbp").arg(device->id));
  QString path = QFileDialog::getSaveFileName(
    this, DisplayLanguage::text(QStringLiteral("Save FlyingBytesPro Project")), suggested,
    DisplayLanguage::text(QStringLiteral("FlyingBytesPro Project (*.fbp);;All Files (*.*)")));
  if (path.isEmpty()) return;
  if (!path.toLower().endsWith(QStringLiteral(".fbp"))) path += QStringLiteral(".fbp");
  rememberFileDirectory(path);

  FlyingBytesProject project;
  project.deviceId = device->id;
  project.clockId = currentClockId();
  project.flash = m_flashUi.model->imageCopy();
  project.eeprom = m_eepromUi.model->imageCopy();
  for (QLineEdit* edit : m_preFuseEdits) {
    bool ok = false;
    const int value = edit->text().toInt(&ok, 16);
    project.preFuses.append(ok && edit->text().size() == 2 ? value : -1);
  }
  for (QLineEdit* edit : m_fuseEdits) {
    bool ok = false;
    const int value = edit->text().toInt(&ok, 16);
    project.finalFuses.append(ok && edit->text().size() == 2 ? value : -1);
  }
  bool lockOk = false;
  project.lockValue = lockValueFromUi(lockOk);
  if (!lockOk) project.lockValue = -1;
  for (QCheckBox* check : m_taskChecks) {
    project.taskSelections.append(check->isChecked());
  }

  QString error;
  if (!ProjectFile::save(path, project, error)) {
    showResult(false, QStringLiteral("Project save failed: %1").arg(error));
    return;
  }
  appendLog(QStringLiteral("Project saved to %1.").arg(path));
  setMiniLog(QStringLiteral("Project saved — Flash CRC 0x%1, EEPROM CRC 0x%2")
    .arg(m_flashMeter->crc(), 4, 16, QLatin1Char('0'))
    .arg(m_eepromMeter->crc(), 4, 16, QLatin1Char('0')).toUpper(), true);
}

void MainWindow::loadProject() {
  startProgress(ProgressState::Idle);
  if (!confirmDiscardModifiedBuffers()) return;
  const QString path = QFileDialog::getOpenFileName(
    this, DisplayLanguage::text(QStringLiteral("Load FlyingBytesPro Project")), lastFileDirectory(),
    DisplayLanguage::text(QStringLiteral("FlyingBytesPro Project (*.fbp);;All Files (*.*)")));
  if (path.isEmpty()) return;
  rememberFileDirectory(path);

  FlyingBytesProject project;
  QString error;
  if (!ProjectFile::load(path, project, error)) {
    showResult(false, QStringLiteral("Project load failed: %1").arg(error));
    return;
  }
  const AvrDevice* device = m_database.byId(project.deviceId);
  if (!device) {
    showResult(false, QStringLiteral("Project MCU '%1' is not present in this device database.")
      .arg(project.deviceId));
    return;
  }
  if (project.flash.capacity() != device->flashSize
      || project.eeprom.capacity() != device->eepromSize) {
    showResult(false, QStringLiteral("The project memory geometry does not match %1.")
      .arg(device->name));
    return;
  }

  const int index = m_deviceCombo->findData(device->id);
  m_deviceCombo->blockSignals(true);
  m_deviceCombo->setCurrentIndex(index);
  m_deviceCombo->blockSignals(false);
  m_previousDeviceIndex = index;
  {
    QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
    settings.setValue(QStringLiteral("selectedDeviceId"), device->id);
    settings.setValue(QStringLiteral("sckClockId"), project.clockId);
    settings.setValue(QStringLiteral("sckClockSchema"), 2);
  }
  m_flashUi.model->setImage(project.flash);
  m_eepromUi.model->setImage(project.eeprom);
  m_flashUi.model->setClean();
  m_eepromUi.model->setClean();
  m_sckDial->setClockId(project.clockId);
  updateFuseUi(*device);

  for (int i = 0; i < m_preFuseEdits.size(); ++i) {
    m_preFuseEdits[i]->setText(i < project.preFuses.size()
      ? byteText(project.preFuses[i]) : QString());
    m_fuseEdits[i]->setText(i < project.finalFuses.size()
      ? byteText(project.finalFuses[i]) : QString());
  }
  m_lockEdit->setText(byteText(project.lockValue));
  updateFuseButtonTexts();
  for (int i = 0; i < m_taskChecks.size(); ++i) {
    if (i < project.taskSelections.size()) {
      m_taskChecks[i]->setChecked(project.taskSelections[i]);
    }
  }
  m_signatureLabel->setText(deviceDetailsText(*device));
  updateMemoryIndicators();
  appendLog(QStringLiteral("Project loaded from %1 for %2.").arg(path, device->name));
  saveFuseValuesForDeviceId(device->id);
  saveTaskSelections();
  setMiniLog(QStringLiteral("Project loaded — %1 — Flash CRC 0x%2")
    .arg(device->name)
    .arg(QStringLiteral("%1").arg(m_flashMeter->crc(), 4, 16, QLatin1Char('0')).toUpper()), true);
}

QVector<int> MainWindow::fuseValuesFromEdits(const QVector<QLineEdit*>& edits,
                                             bool& ok) const {
  QVector<int> values;
  ok = true;
  const AvrDevice* device = currentDevice();
  if (!device) {
    ok = false;
    return values;
  }
  for (int i = 0; i < device->fuseCount(); ++i) {
    bool fieldOk = false;
    const QString text = edits.at(i)->text().trimmed();
    const int value = text.toInt(&fieldOk, 16);
    if (!fieldOk || text.size() != 2 || value < 0 || value > 0xFF) {
      ok = false;
      return {};
    }
    values.append(value);
  }
  return values;
}

bool MainWindow::confirmMemoryWrite() const {
  return !m_confirmMemoryWritesCheck || m_confirmMemoryWritesCheck->isChecked();
}

bool MainWindow::ignoreMcuSignatureMatching() const {
  return m_ignoreMcuSignatureMatchingCheck
    && m_ignoreMcuSignatureMatchingCheck->isChecked();
}

bool MainWindow::fullFlashRead() const {
  return m_flashReadModeCombo
    && m_flashReadModeCombo->currentData().toString() == QStringLiteral("full");
}

int MainWindow::lockValueFromUi(bool& ok) const {
  const QString text = m_lockEdit->text().trimmed();
  const int value = text.toInt(&ok, 16);
  ok = ok && text.size() == 2 && value >= 0 && value <= 0xFF;
  return value;
}

QString MainWindow::lastFileDirectory() const {
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  const QString directory = settings.value(
    QStringLiteral("lastFileDirectory"), QDir::homePath()).toString();
  return QDir(directory).exists() ? directory : QDir::homePath();
}

void MainWindow::rememberFileDirectory(const QString& path) const {
  const QString directory = QFileInfo(path).absolutePath();
  if (directory.isEmpty()) return;
  QSettings settings(QStringLiteral("Flyandance"), QStringLiteral("FlyingBytesPro"));
  settings.setValue(QStringLiteral("lastFileDirectory"), directory);
}

const AvrDevice* MainWindow::chooseSharedDevice(
    const QByteArray& signature,
    const QVector<const AvrDevice*>& matches) {
  if (matches.isEmpty()) return nullptr;
  if (matches.size() == 1) return matches.first();

  QDialog dialog(this);
  dialog.setWindowTitle(DisplayLanguage::text(QStringLiteral("Select MCU")));
  dialog.setModal(true);
  auto* layout = new QVBoxLayout(&dialog);
  auto* image = new QLabel(&dialog);
  image->setPixmap(QPixmap(QStringLiteral(":/icons/MCU_Search.png")).scaled(
    78, 84, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  image->setAlignment(Qt::AlignCenter);
  layout->addWidget(image, 0, Qt::AlignHCenter);

  auto* label = new QLabel(
    DisplayLanguage::text(QStringLiteral("Signature %1 matches multiple AVR devices.\nSelect the exact MCU:"))
      .arg(QString::fromLatin1(signature.toHex(' ').toUpper())), &dialog);
  label->setWordWrap(true);
  layout->addWidget(label);

  auto* buttons = new QGridLayout;
  buttons->setHorizontalSpacing(8);
  buttons->setVerticalSpacing(8);
  const AvrDevice* chosen = nullptr;
  for (int i = 0; i < matches.size(); ++i) {
    const AvrDevice* device = matches.at(i);
    auto* button = new QPushButton(mcuClassIcon(device->name), device->name, &dialog);
    button->setIconSize(QSize(18, 18));
    button->setMinimumSize(155, 36);
    connect(button, &QPushButton::clicked, &dialog, [&, device] {
      chosen = device;
      dialog.accept();
    });
    buttons->addWidget(button, i / 3, i % 3);
  }
  layout->addLayout(buttons);
  dialog.exec();
  return chosen;
}

void MainWindow::startTaskSequence() {
  if (m_busy || m_taskSequenceActive) return;
  const AvrDevice* device = currentDevice();
  if (!device) return;

  m_taskQueue.clear();
  const bool anySelected = std::any_of(m_taskChecks.cbegin(), m_taskChecks.cend(),
    [](const QCheckBox* check) { return check && check->isChecked(); });
  const bool flashHasData = m_flashUi.model->image().definedCount() > 0;
  const bool eepromHasData = m_eepromUi.model->image().definedCount() > 0;
  const QVector<TaskStep> order{
    TaskStep::ConfirmSignature, TaskStep::EraseChip, TaskStep::PrewriteFuses,
    TaskStep::ProgramFlash, TaskStep::ProgramEeprom, TaskStep::VerifyFlash,
    TaskStep::VerifyEeprom, TaskStep::ProgramFinalFuses, TaskStep::ProgramLock
  };
  for (int i = 0; i < m_taskChecks.size(); ++i) {
    if ((i == 4 || i == 6) && (device->eepromSize == 0 || !eepromHasData)) continue;
    if ((i == 3 || i == 5) && !flashHasData) continue;
    if (i == 1) {
      if (m_taskChecks[1]->isChecked()
          || (m_taskChecks[3]->isChecked() && flashHasData)) {
        m_taskQueue.append(TaskStep::EraseChip);
      }
      continue;
    }
    if (m_taskChecks[i]->isChecked()) m_taskQueue.append(order[i]);
  }
  if (m_taskQueue.isEmpty()) {
    if (!anySelected) {
      showResult(false, QStringLiteral("No tasks are selected."));
    }
    return;
  }

  if (m_taskChecks[2]->isChecked()) {
    bool ok = false;
    fuseValuesFromEdits(m_preFuseEdits, ok);
    if (!ok) {
      showResult(false, QStringLiteral("The pre-write fuse values are invalid."));
      return;
    }
  }
  if (m_taskChecks[7]->isChecked()) {
    bool ok = false;
    fuseValuesFromEdits(m_fuseEdits, ok);
    if (!ok) {
      showResult(false, QStringLiteral("The fuse values are invalid."));
      return;
    }
  }
  if (m_taskChecks[8]->isChecked()) {
    bool ok = false;
    lockValueFromUi(ok);
    if (!ok) {
      showResult(false, QStringLiteral("The lock value is invalid."));
      return;
    }
  }

  m_taskSequenceActive = true;
  m_taskQueueIndex = 0;
  m_activeTask = TaskStep::None;
  m_taskSummaryItems.clear();
  m_taskMiniLogTimestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
  m_taskMiniLogBlockNumber = -1;
  setTaskMiniLogLine(QString(), true);
  m_taskElapsedTimer.start();
  appendLog(QStringLiteral("===== Automatic Task Sequence: %1 =====")
    .arg(device->name));
  runNextTask();
}

void MainWindow::runNextTask() {
  if (!m_taskSequenceActive) return;
  if (m_taskQueueIndex >= m_taskQueue.size()) {
    stopTaskSequence(QStringLiteral("All selected tasks completed successfully."), true);
    return;
  }

  const AvrDevice* device = currentDevice();
  if (!device) {
    stopTaskSequence(QStringLiteral("No MCU is selected."), false);
    return;
  }
  m_activeTask = m_taskQueue.at(m_taskQueueIndex);
  appendLog(QStringLiteral("Task %1/%2: %3")
    .arg(m_taskQueueIndex + 1)
    .arg(m_taskQueue.size())
    .arg(taskName(m_activeTask)));
  ProgressState progressState = ProgressState::Idle;
  if (m_activeTask == TaskStep::ProgramFlash) {
    progressState = ProgressState::Flash;
  } else if (m_activeTask == TaskStep::ProgramEeprom) {
    progressState = ProgressState::Eeprom;
  }
  startProgress(progressState);
  if (m_activeTask == TaskStep::ProgramFlash || m_activeTask == TaskStep::VerifyFlash) {
    startMemoryActivity(true);
  } else if (m_activeTask == TaskStep::ProgramEeprom
             || m_activeTask == TaskStep::VerifyEeprom) {
    startMemoryActivity(false);
  }
  setBusy(true, taskName(m_activeTask));

  switch (m_activeTask) {
  case TaskStep::ConfirmSignature:
    if (ignoreMcuSignatureMatching()) {
      completeTaskStep(true, QStringLiteral(
        "Signature matching skipped by the Settings option."));
    } else {
      Q_EMIT requestDetect(currentClockId());
    }
    break;
  case TaskStep::EraseChip:
    Q_EMIT requestErase(*device, currentClockId());
    break;
  case TaskStep::PrewriteFuses: {
    bool ok = false;
    const QVector<int> values = fuseValuesFromEdits(m_preFuseEdits, ok);
    if (!ok) {
      completeTaskStep(false, QStringLiteral("Invalid pre-write fuse values."));
    } else {
      Q_EMIT requestWriteFuses(*device, values, currentClockId());
    }
    break;
  }
  case TaskStep::ProgramFlash:
    Q_EMIT requestWriteFlash(*device,
      m_flashUi.model->imageCopy(),
      false, false, currentClockId());
    break;
  case TaskStep::ProgramEeprom:
    Q_EMIT requestWriteEeprom(*device,
      m_eepromUi.model->imageCopy(),
      false, currentClockId());
    break;
  case TaskStep::VerifyFlash:
    Q_EMIT requestVerifyFlash(*device, m_flashUi.model->imageCopy(), currentClockId());
    break;
  case TaskStep::VerifyEeprom:
    Q_EMIT requestVerifyEeprom(*device, m_eepromUi.model->imageCopy(), currentClockId());
    break;
  case TaskStep::ProgramFinalFuses: {
    bool ok = false;
    const QVector<int> values = fuseValuesFromEdits(m_fuseEdits, ok);
    if (!ok) {
      completeTaskStep(false, QStringLiteral("Invalid fuse values."));
    } else {
      Q_EMIT requestWriteFuses(*device, values, currentClockId());
    }
    break;
  }
  case TaskStep::ProgramLock: {
    bool ok = false;
    const int value = lockValueFromUi(ok);
    if (!ok) {
      completeTaskStep(false, QStringLiteral("Invalid lock byte."));
    } else {
      Q_EMIT requestWriteLock(*device, value, currentClockId());
    }
    break;
  }
  case TaskStep::None:
    completeTaskStep(false, QStringLiteral("Internal task-sequence error."));
    break;
  }
}

void MainWindow::completeTaskStep(bool success, const QString& message) {
  if (!m_taskSequenceActive) return;
  completeProgressResult(success);
  const QString taskResultText = DisplayLanguage::text(QStringLiteral("%1 Task %2: %3"))
    .arg(success ? QStringLiteral("A-Okay ☆☆☆")
                 : QStringLiteral("Error! ★★★"),
         taskName(m_activeTask), DisplayLanguage::text(message));
  appendLog(taskResultText);
  if (!success) {
    stopTaskSequence(DisplayLanguage::text(QStringLiteral("%1 failed: %2"))
      .arg(taskName(m_activeTask), DisplayLanguage::text(message)), false);
    return;
  }
  const QString summaryItem = taskSummaryItem(m_activeTask, message);
  bool mergedVerify = false;
  if (m_activeTask == TaskStep::VerifyFlash || m_activeTask == TaskStep::VerifyEeprom) {
    const bool flash = m_activeTask == TaskStep::VerifyFlash;
    const QString memoryName = flash ? QStringLiteral("Flash") : QStringLiteral("EEPROM");
    const QRegularExpression bytesPattern(QStringLiteral("(\\d+)\\s+B"));
    for (int i = m_taskSummaryItems.size() - 1; i >= 0; --i) {
      if (!m_taskSummaryItems[i].contains(memoryName)) continue;
      const QRegularExpressionMatch match = bytesPattern.match(m_taskSummaryItems[i]);
      if (match.hasMatch()) {
        const QString canonical = flash
          ? QStringLiteral("[Flash %1 B written + verified]")
          : QStringLiteral("[EEPROM %1 B written + verified]");
        m_taskSummaryItems[i] = DisplayLanguage::text(canonical.arg(match.captured(1)));
      } else {
        m_taskSummaryItems[i] = DisplayLanguage::text(flash
          ? QStringLiteral("[Flash written + verified]")
          : QStringLiteral("[EEPROM written + verified]"));
      }
      mergedVerify = true;
      break;
    }
  }
  if (!mergedVerify && !summaryItem.isEmpty()) {
    m_taskSummaryItems.append(summaryItem);
  }
  setTaskMiniLogLine(m_taskSummaryItems.join(QLatin1Char(' ')), true);
  ++m_taskQueueIndex;
  m_activeTask = TaskStep::None;
  setBusy(false);
  QTimer::singleShot(80, this, &MainWindow::runNextTask);
}

void MainWindow::stopTaskSequence(const QString& message, bool success) {
  const qint64 elapsedMs = m_taskElapsedTimer.isValid()
    ? m_taskElapsedTimer.elapsed() : 0;
  m_taskElapsedTimer.invalidate();
  const QString elapsed = QStringLiteral("%1s")
    .arg(static_cast<double>(elapsedMs) / 1000.0, 0, 'f', 2);

  QString summary;
  if (success) {
    const QString tasks = m_taskSummaryItems.isEmpty()
      ? QStringLiteral("[No work]")
      : m_taskSummaryItems.join(QLatin1Char(' '));
    summary = QStringLiteral("%1 completed in %2.")
      .arg(tasks, elapsed);
  } else {
    const QString tasks = m_taskSummaryItems.isEmpty()
      ? QString()
      : m_taskSummaryItems.join(QLatin1Char(' ')) + QLatin1Char(' ');
    summary = QStringLiteral("%1[Failed: %2] after %3.")
      .arg(tasks, message, elapsed);
  }

  m_taskSequenceActive = false;
  m_activeTask = TaskStep::None;
  setBusy(false);
  completeProgressResult(success);
  appendLog(QStringLiteral("%1 %2")
    .arg(success ? QStringLiteral("A-Okay ☆☆☆")
                 : QStringLiteral("Error! ★★★"),
         summary));
  setTaskMiniLogLine(summary, success);
  m_taskMiniLogTimestamp.clear();
  m_taskMiniLogBlockNumber = -1;
}

QString MainWindow::taskSummaryItem(TaskStep step, const QString& message) const {
  const QRegularExpression bytesPattern(QStringLiteral("^(\\d+) bytes (Flash|EEPROM) written"));
  const QRegularExpressionMatch bytesMatch = bytesPattern.match(message);
  switch (step) {
  case TaskStep::ConfirmSignature:
    if (m_taskQueueIndex + 1 < m_taskQueue.size()) return QString();
    return DisplayLanguage::text(message.contains(QStringLiteral("skipped"), Qt::CaseInsensitive)
      ? QStringLiteral("[MCU check skipped]")
      : QStringLiteral("[MCU checked]"));
  case TaskStep::EraseChip:
    return m_taskQueueIndex + 1 < m_taskQueue.size()
      ? QString() : DisplayLanguage::text(QStringLiteral("[Chip erased]"));
  case TaskStep::PrewriteFuses:
    return m_taskQueueIndex + 1 < m_taskQueue.size()
      ? QString() : DisplayLanguage::text(QStringLiteral("[Pre-write fuses]"));
  case TaskStep::ProgramFlash:
    return bytesMatch.hasMatch()
      ? DisplayLanguage::text(QStringLiteral("[Flash %1 B written]").arg(bytesMatch.captured(1)))
      : DisplayLanguage::text(QStringLiteral("[Flash written]"));
  case TaskStep::ProgramEeprom:
    return bytesMatch.hasMatch()
      ? DisplayLanguage::text(QStringLiteral("[EEPROM %1 B written]").arg(bytesMatch.captured(1)))
      : DisplayLanguage::text(QStringLiteral("[EEPROM written]"));
  case TaskStep::VerifyFlash:
    return DisplayLanguage::text(QStringLiteral("[Flash verified]"));
  case TaskStep::VerifyEeprom:
    return DisplayLanguage::text(QStringLiteral("[EEPROM verified]"));
  case TaskStep::ProgramFinalFuses:
    return DisplayLanguage::text(QStringLiteral("[Fuses programmed]"));
  case TaskStep::ProgramLock:
    return DisplayLanguage::text(QStringLiteral("[Lock programmed]"));
  case TaskStep::None:
    break;
  }
  return QString();
}

QString MainWindow::taskName(TaskStep step) const {
  switch (step) {
  case TaskStep::ConfirmSignature: return DisplayLanguage::text(QStringLiteral("Confirm Signature"));
  case TaskStep::EraseChip: return DisplayLanguage::text(QStringLiteral("Erase Chip"));
  case TaskStep::PrewriteFuses: return DisplayLanguage::text(QStringLiteral("Pre-Write Fuses"));
  case TaskStep::ProgramFlash: return DisplayLanguage::text(QStringLiteral("Program Flash"));
  case TaskStep::ProgramEeprom: return DisplayLanguage::text(QStringLiteral("Program EEPROM"));
  case TaskStep::VerifyFlash: return DisplayLanguage::text(QStringLiteral("Verify Flash"));
  case TaskStep::VerifyEeprom: return DisplayLanguage::text(QStringLiteral("Verify EEPROM"));
  case TaskStep::ProgramFinalFuses: return DisplayLanguage::text(QStringLiteral("Write Final Fuses"));
  case TaskStep::ProgramLock: return DisplayLanguage::text(QStringLiteral("Lock Fuse"));
  case TaskStep::None: return DisplayLanguage::text(QStringLiteral("None"));
  }
  return DisplayLanguage::text(QStringLiteral("Unknown task"));
}

void MainWindow::handleProbeFinished(bool success, const QString& message,
                                     quint32 capabilities, bool quiet) {
  Q_UNUSED(capabilities)
  if (quiet) {
    m_autoProbeInProgress = false;
    if (success) {
      const bool changed = !m_usbConnected;
      updateUsbStatus(true, QStringLiteral("Connected"));
      if (changed) {
        QString onlineText = DisplayLanguage::text(QStringLiteral("Programmer Online :)"));
        onlineText.remove(QStringLiteral(" :)"));
        appendLog(QStringLiteral("A-Okay ☆☆☆ %1").arg(onlineText));
        setMiniLog(QStringLiteral("Programmer Online :)"), true);
      }
    } else if (m_usbConnected) {
      updateUsbStatus(false, QStringLiteral("Disconnected"));
      QString offlineText = DisplayLanguage::text(QStringLiteral("Programmer Offline :("));
      offlineText.remove(QStringLiteral(" :("));
      appendLog(QStringLiteral("A-Okay ☆☆☆ %1").arg(offlineText));
      setMiniLog(QStringLiteral("Programmer Offline :("), true);
    }
    return;
  }

  setBusy(false);
  updateUsbStatus(success, success ? QStringLiteral("Connected") : QStringLiteral("Not found"));
  showResult(success, message);
}

void MainWindow::handleSignatureFinished(bool success, const QString& message,
                                         const QByteArray& signature) {
  setBusy(false);
  if (m_signatureCheckOnly) {
    m_signatureCheckOnly = false;
    const AvrDevice* selected = currentDevice();
    if (!success) {
      showResult(false, message);
      return;
    }
    if (selected
        && QByteArrayView(selected->signature) == QByteArrayView(signature)) {
      m_signatureLabel->setText(deviceDetailsText(*selected));
      showResult(true, QStringLiteral("MCU signature matched: %2 — ID: [%1]")
        .arg(QString::fromLatin1(signature.toHex(' ').toUpper()), selected->name));
    } else {
      showResult(false, QStringLiteral(
        "MCU signature mismatch: read [%1], selected %2 — ID: [%3].")
        .arg(QString::fromLatin1(signature.toHex(' ').toUpper()),
             selected ? selected->name : QStringLiteral("MCU"),
             selected ? selected->signatureText() : QStringLiteral("-- -- --")));
    }
    return;
  }
  if (m_taskSequenceActive && m_activeTask == TaskStep::ConfirmSignature) {
    const AvrDevice* selected = currentDevice();
    const bool matches = success && selected
      && QByteArrayView(selected->signature) == QByteArrayView(signature);
    if (matches) {
      m_signatureLabel->setText(deviceDetailsText(*selected));
      completeTaskStep(true, QStringLiteral("Signature matched: %2 — ID: [%1]")
        .arg(selected->signatureText(), selected->name));
    } else {
      completeTaskStep(false, success
        ? QStringLiteral("Signature mismatch: read [%1], selected %2 — ID: [%3].")
            .arg(QString::fromLatin1(signature.toHex(' ').toUpper()),
                 selected ? selected->name : QStringLiteral("MCU"),
                 selected ? selected->signatureText() : QStringLiteral("-- -- --"))
        : message);
    }
    return;
  }

  if (!success) {
    showResult(false, message);
    return;
  }

  const QVector<const AvrDevice*> matches =
    m_database.bySignatureDetectionCandidates(signature);
  if (matches.isEmpty()) {
    showResult(false, QStringLiteral("Unknown MCU signature: [%1].")
      .arg(QString::fromLatin1(signature.toHex(' ').toUpper())));
    return;
  }

  const AvrDevice* chosen = chooseSharedDevice(signature, matches);
  if (!chosen) return;

  const int index = m_deviceCombo->findData(chosen->id);
  if (index >= 0 && index != m_deviceCombo->currentIndex()) {
    m_deviceChangeIsDetection = true;
    m_deviceCombo->setCurrentIndex(index);
    m_deviceChangeIsDetection = false;
  }
  m_signatureLabel->setText(deviceDetailsText(*chosen));
  showResult(true, QStringLiteral("MCU detected: %1 — ID: [%2]")
    .arg(chosen->name, QString::fromLatin1(signature.toHex(' ').toUpper())));
}

void MainWindow::handleImageFinished(const QString& operation, bool success,
                                     const QString& message,
                                     const FirmwareImage& image) {
  if (operation == QStringLiteral("read-flash")
      || operation == QStringLiteral("read-eeprom")) {
    stopMemoryActivity();
  }
  setBusy(false);
  if (success) {
    if (operation == QStringLiteral("read-flash")) {
      m_flashUi.model->setImage(image);
      m_flashUi.model->setClean();
    } else if (operation == QStringLiteral("read-eeprom")) {
      m_eepromUi.model->setImage(image);
      m_eepromUi.model->setClean();
    }
    updateMemoryIndicators();
  }
  if (success) {
    const bool flash = operation == QStringLiteral("read-flash");
    const quint16 crc = flash ? m_flashMeter->crc() : m_eepromMeter->crc();
    showResult(true, QStringLiteral("%1 — CRC 0x%2")
      .arg(message, QStringLiteral("%1")
        .arg(crc, 4, 16, QLatin1Char('0')).toUpper()));
  } else {
    showResult(false, message);
  }
}

void MainWindow::handleOperationFinished(const QString& operation, bool success,
                                         const QString& message) {
  if (operation == QStringLiteral("write-flash")
      || operation == QStringLiteral("verify-flash")
      || operation == QStringLiteral("blank-flash")
      || operation == QStringLiteral("write-eeprom")
      || operation == QStringLiteral("verify-eeprom")
      || operation == QStringLiteral("blank-eeprom")) {
    stopMemoryActivity();
  }
  setBusy(false);
  if (m_taskSequenceActive) {
    completeTaskStep(success, message);
    return;
  }

  if (m_activeFuseEditor
      && (operation == QStringLiteral("write-fuses")
          || operation == QStringLiteral("write-lock"))) {
    completeProgressResult(success);
    m_activeFuseEditor->setStatus(message, success);
    return;
  }

  showResult(success, message);
}

void MainWindow::handleFusesFinished(bool success, const QString& message,
                                     const QVector<int>& values, int lockValue) {
  setBusy(false);
  if (success) {
    for (int i = 0; i < m_fuseEdits.size(); ++i) {
      const QString text = i < values.size() ? byteText(values.at(i)) : QStringLiteral("FF");
      m_fuseEdits[i]->setText(text);
      m_preFuseEdits[i]->setText(text);
    }
    if (lockValue >= 0) m_lockEdit->setText(byteText(lockValue));
    updateFuseButtonTexts();
    const AvrDevice* device = currentDevice();
    if (device) saveFuseValuesForDeviceId(device->id);
  }

  if (m_activeFuseEditor) {
    completeProgressResult(success);
    if (success) {
      m_activeFuseEditor->setReadValues(values, lockValue);
    } else {
      m_activeFuseEditor->setStatus(message, false);
    }
    return;
  }

  if (success) {
    const QString low = values.size() > 0 ? QStringLiteral("0x%1").arg(byteText(values[0]))
                                         : QStringLiteral("N/A");
    const QString high = values.size() > 1 ? QStringLiteral("0x%1").arg(byteText(values[1]))
                                          : QStringLiteral("N/A");
    const QString extended = values.size() > 2 ? QStringLiteral("0x%1").arg(byteText(values[2]))
                                              : QStringLiteral("N/A");
    const QString lock = lockValue >= 0 ? QStringLiteral("0x%1").arg(byteText(lockValue))
                                        : QStringLiteral("N/A");
    showResult(true, QStringLiteral("Fuses read — Low %2, High %1, Extended %3, Lock %4")
      .arg(high, low, extended, lock));
  } else {
    showResult(false, message);
  }
}

void MainWindow::appendLog(const QString& message) {
  if (!m_log) return;
  QString displayMessage;
  if (message.startsWith(QStringLiteral("===== "))
      && message.endsWith(QStringLiteral(" ====="))
      && message.size() > 12) {
    const QString operation = message.mid(6, message.size() - 12);
    displayMessage = QStringLiteral("===== %1 =====").arg(DisplayLanguage::text(operation));
  } else {
    displayMessage = DisplayLanguage::text(message);
  }
  const bool separator = displayMessage.startsWith(QStringLiteral("===== "));
  const bool technical = displayMessage.startsWith(QStringLiteral("USB CTRL"))
    || displayMessage.startsWith(QStringLiteral("USBASP BULK"))
    || displayMessage.startsWith(QStringLiteral("ISP CMD"));
  if (technical) displayMessage.prepend(QStringLiteral("  "));

  const QString line = QStringLiteral("[%1] %2")
    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")), displayMessage);
  QTextCursor cursor = m_log->textCursor();
  cursor.movePosition(QTextCursor::End);
  if (!m_log->toPlainText().isEmpty()) cursor.insertBlock();
  QTextCharFormat format;
  const QString trimmedMessage = displayMessage.trimmed();
  if (trimmedMessage.startsWith(QStringLiteral("Error!"), Qt::CaseInsensitive)
      || trimmedMessage.startsWith(QStringLiteral("ERROR"), Qt::CaseInsensitive)) {
    format.setForeground(QColor(QStringLiteral("#CD5C5C")));
  } else {
    format.setForeground(QColor(QStringLiteral("#000000")));
    if (separator) format.setFontWeight(QFont::Bold);
  }
  cursor.insertText(line, format);
  m_log->setTextCursor(cursor);
  m_log->ensureCursorVisible();
}

void MainWindow::setProgress(int percent) {
  if (!m_progress) return;
  if (m_progressState != ProgressState::Flash
      && m_progressState != ProgressState::Eeprom) return;
  m_progress->setValue(std::clamp(percent, 0, 100));
}

void MainWindow::autoProbeTick() {
  if (m_demoMode || m_busy || m_autoProbeInProgress || !m_controller) return;
  m_autoProbeInProgress = true;
  Q_EMIT requestProbe(currentClockId(), true);
}

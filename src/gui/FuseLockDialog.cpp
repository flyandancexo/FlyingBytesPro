// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/FuseLockDialog.h"

#include "gui/DisplayLanguage.h"

#include <QAbstractItemView>
#include <QCloseEvent>
#include <QFont>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int kByteColumnWidth = 142;
constexpr int kRawValueColumnWidth = 82;
constexpr int kRawImageColumnWidth = 60;
constexpr int kSectionInnerMargin = 5;
constexpr int kSectionColumnSpacing = 3;
constexpr int kGroupFrameAllowance = 12;
constexpr int kFuseSectionWidth =
  kGroupFrameAllowance + (kSectionInnerMargin * 2)
  + (kByteColumnWidth * 3) + (kSectionColumnSpacing * 2);
constexpr int kLockSectionWidth =
  kGroupFrameAllowance + (kSectionInnerMargin * 2) + kByteColumnWidth;

QString normalizeOptionText(QString text) {
  text.replace(QRegularExpression(QStringLiteral("\\s+")),
               QStringLiteral(" "));
  text.replace(
    QRegularExpression(
      QStringLiteral("Ext\\. RC Osc\\. - 0\\.9 MHz"),
      QRegularExpression::CaseInsensitiveOption),
    QStringLiteral("Ext. RC Osc. 0.1 MHz - 0.9 MHz"));
  return text.trimmed();
}

QString byteText(int value) {
  return QStringLiteral("%1")
    .arg(value & 0xFF, 2, 16, QLatin1Char('0')).toUpper();
}

QString byteTitle(int byteIndex) {
  switch (byteIndex) {
  case 0: return DisplayLanguage::text(QStringLiteral("Low Fuse"));
  case 1: return DisplayLanguage::text(QStringLiteral("High Fuse"));
  case 2: return DisplayLanguage::text(QStringLiteral("Extended Fuse"));
  case 3: return DisplayLanguage::text(QStringLiteral("Lock Fuse"));
  default: return DisplayLanguage::text(QStringLiteral("Byte"));
  }
}

int parseHexByte(QString text, bool& ok) {
  text = text.trimmed();
  if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) {
    text.remove(0, 2);
  }
  const int value = text.toInt(&ok, 16);
  if (!ok || value < 0 || value > 0xFF) {
    ok = false;
    return 0;
  }
  return value;
}

} // namespace

FuseLockDialog::FuseLockDialog(const AvrDevice& device,
                               const QVector<int>& fuseValues,
                               int lockValue,
                               const QJsonObject& metadata,
                               bool prewrite,
                               QWidget* parent)
    : QDialog(parent), m_device(device), m_metadata(metadata) {
  setWindowTitle(prewrite
    ? QStringLiteral("Pre-Write Fuse & Lock")
    : QStringLiteral("Fuse & Lock"));
  setModal(true);
  resize(650, 535);
  setMinimumSize(610, 480);

  m_bitButtons.resize(4);
  for (QVector<QPushButton*>& bits : m_bitButtons) bits.resize(8);
  for (int i = 0; i < std::min(3, static_cast<int>(fuseValues.size())); ++i) {
    m_values[i] = fuseValues.at(i) & 0xFF;
  }
  m_values[LockByte] = lockValue >= 0 ? lockValue & 0xFF : 0xFF;

  auto* mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(6, 6, 6, 6);
  mainLayout->setSpacing(4);

  auto* rawRow = new QHBoxLayout;
  rawRow->setSpacing(6);

  rawRow->addStretch();

  auto* fuseBox = new QGroupBox(QStringLiteral("Fuses"), this);
  fuseBox->setFixedWidth(kFuseSectionWidth);
  auto* fuseLayout = new QGridLayout(fuseBox);
  fuseLayout->setContentsMargins(kSectionInnerMargin, 7,
                                 kSectionInnerMargin, 5);
  fuseLayout->setHorizontalSpacing(2);
  fuseLayout->setVerticalSpacing(3);
  const QVector<int> fuseOrder{HighByte, LowByte, ExtendedByte};
  const QVector<int> valueColumns{0, 2, 4};
  const QVector<Qt::Alignment> valueAlignments{
    Qt::AlignRight | Qt::AlignVCenter,
    Qt::AlignHCenter | Qt::AlignVCenter,
    Qt::AlignLeft | Qt::AlignVCenter
  };
  for (int index = 0; index < fuseOrder.size(); ++index) {
    const int byteIndex = fuseOrder.at(index);
    const int column = valueColumns.at(index);
    const bool supported = byteIndex < m_device.fuseCount();
    auto* label = new QLabel(byteTitle(byteIndex), fuseBox);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet(QStringLiteral("font-weight: 700;"));
    fuseLayout->setColumnMinimumWidth(column, kRawValueColumnWidth);
    fuseLayout->addWidget(label, 0, column, valueAlignments.at(index));
    m_valueEdits[byteIndex] = createValueEdit(byteIndex, supported, fuseBox);
    fuseLayout->addWidget(m_valueEdits[byteIndex], 1, column,
                          valueAlignments.at(index));
  }

  const auto addFuseImage = [fuseBox, fuseLayout](const QString& resource,
                                                   int column) {
    fuseLayout->setColumnMinimumWidth(column, kRawImageColumnWidth);
    auto* image = new QLabel(fuseBox);
    image->setPixmap(QPixmap(resource).scaled(
      54, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    image->setFixedSize(58, 58);
    image->setAlignment(Qt::AlignCenter);
    fuseLayout->addWidget(image, 0, column, 3, 1, Qt::AlignCenter);
  };
  addFuseImage(QStringLiteral(":/icons/Fuse_High.png"), 1);
  addFuseImage(QStringLiteral(":/icons/Fuse_Low.png"), 3);

  auto* readFusesButton = new QPushButton(QStringLiteral("Read"), fuseBox);
  auto* defaultButton = new QPushButton(QStringLiteral("Default"), fuseBox);
  m_writeFusesButton = new QPushButton(QStringLiteral("Write"), fuseBox);
  readFusesButton->setFixedWidth(52);
  defaultButton->setFixedWidth(64);
  m_writeFusesButton->setFixedWidth(52);
  for (QPushButton* button : {readFusesButton, defaultButton,
                              m_writeFusesButton}) {
    button->setFixedHeight(25);
    button->setFocusPolicy(Qt::NoFocus);
  }
  const QString readButtonStyle = QStringLiteral(
    "QPushButton { color: #1766B5; font-weight: 700; padding: 2px 4px; }"
    "QPushButton:hover { background: #eaf2fb; border-color: #6d9fd0; }"
    "QPushButton:pressed { background: #dce9f6; }");
  const QString defaultButtonStyle = QStringLiteral(
    "QPushButton { color: #3EB489; font-weight: 700; padding: 2px 4px; }"
    "QPushButton:hover { background: #e9f8f2; border-color: #3EB489; }"
    "QPushButton:pressed { background: #d9f1e8; }");
  const QString writeButtonStyle = QStringLiteral(
    "QPushButton { color: #CD5C5C; font-weight: 700; padding: 2px 4px; }"
    "QPushButton:hover { background: #fbecec; border-color: #CD5C5C; }"
    "QPushButton:pressed { background: #f3dddd; }");
  readFusesButton->setStyleSheet(readButtonStyle);
  defaultButton->setStyleSheet(defaultButtonStyle);
  m_writeFusesButton->setStyleSheet(writeButtonStyle);
  m_writeFusesButton->setEnabled(std::any_of(
    m_device.fuseProgramMasks.cbegin(), m_device.fuseProgramMasks.cend(),
    [](quint8 mask) { return mask != 0; }));
  fuseLayout->addWidget(readFusesButton, 2, 0,
                        Qt::AlignRight | Qt::AlignVCenter);
  fuseLayout->addWidget(defaultButton, 2, 2, Qt::AlignHCenter);
  fuseLayout->addWidget(m_writeFusesButton, 2, 4,
                        Qt::AlignLeft | Qt::AlignVCenter);
  rawRow->addWidget(fuseBox);

  auto* lockBox = new QGroupBox(QStringLiteral("Lock Fuse"), this);
  lockBox->setFixedWidth(kLockSectionWidth);
  auto* lockLayout = new QGridLayout(lockBox);
  lockLayout->setContentsMargins(kSectionInnerMargin + 10, 7,
                                 kSectionInnerMargin, 5);
  lockLayout->setHorizontalSpacing(15);
  lockLayout->setVerticalSpacing(2);
  lockLayout->setColumnMinimumWidth(0, 59);
  lockLayout->setColumnMinimumWidth(1, 60);
  lockLayout->setColumnStretch(2, 1);
  const bool lockSupported = m_device.hasLockByte();
  m_valueEdits[LockByte] = createValueEdit(LockByte, lockSupported, lockBox);
  m_valueEdits[LockByte]->setFixedWidth(46);

  auto* lockImage = new QLabel(lockBox);
  lockImage->setPixmap(QPixmap(QStringLiteral(":/icons/Fuse_Lock.png"))
    .scaled(34, 41, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  lockImage->setFixedSize(42, 43);
  lockImage->setAlignment(Qt::AlignCenter);
  lockLayout->addWidget(lockImage, 0, 0, Qt::AlignCenter);
  lockLayout->addWidget(m_valueEdits[LockByte], 1, 0, Qt::AlignCenter);

  auto* readLockButton = new QPushButton(QStringLiteral("Read"), lockBox);
  m_writeLockButton = new QPushButton(QStringLiteral("Write"), lockBox);
  for (QPushButton* button : {readLockButton, m_writeLockButton}) {
    button->setFixedSize(46, 24);
    button->setFocusPolicy(Qt::NoFocus);
  }
  readLockButton->setStyleSheet(readButtonStyle);
  m_writeLockButton->setStyleSheet(writeButtonStyle);
  m_writeLockButton->setEnabled(m_device.lockProgramMask != 0);
  lockLayout->addWidget(readLockButton, 0, 1, Qt::AlignCenter);
  lockLayout->addWidget(m_writeLockButton, 1, 1, Qt::AlignCenter);
  rawRow->addWidget(lockBox);
  rawRow->addStretch();
  mainLayout->addLayout(rawRow);

  auto* bitsRow = new QHBoxLayout;
  bitsRow->setSpacing(6);

  bitsRow->addStretch();

  auto* fuseBitsBox = new QGroupBox(QStringLiteral("Fuse Bits"), this);
  fuseBitsBox->setFixedWidth(kFuseSectionWidth);
  auto* fuseBitsLayout = new QHBoxLayout(fuseBitsBox);
  fuseBitsLayout->setContentsMargins(kSectionInnerMargin, 7,
                                     kSectionInnerMargin, 5);
  fuseBitsLayout->setSpacing(kSectionColumnSpacing);
  const auto fuseReadMask = [this](int byteIndex) {
    const QJsonObject metadata = fuseMetadata(byteIndex);
    return static_cast<quint8>(metadata.contains(QStringLiteral("readMask"))
      ? metadata.value(QStringLiteral("readMask")).toInt(0xFF)
      : m_device.fuseReadMasks.value(byteIndex, 0xFF));
  };
  const auto fuseProgramMask = [this](int byteIndex) {
    const QJsonObject metadata = fuseMetadata(byteIndex);
    return static_cast<quint8>(metadata.contains(QStringLiteral("programMask"))
      ? metadata.value(QStringLiteral("programMask")).toInt(0)
      : m_device.fuseProgramMasks.value(byteIndex, 0));
  };
  fuseBitsLayout->addWidget(createBitColumn(
    QStringLiteral("High"), HighByte, m_device.fuseCount() >= 2,
    m_device.fuseCount() >= 2 ? fuseReadMask(HighByte) : 0,
    m_device.fuseCount() >= 2 ? fuseProgramMask(HighByte) : 0,
    fuseBitsBox));
  fuseBitsLayout->addWidget(createBitColumn(
    QStringLiteral("Low"), LowByte, m_device.fuseCount() >= 1,
    m_device.fuseCount() >= 1 ? fuseReadMask(LowByte) : 0,
    m_device.fuseCount() >= 1 ? fuseProgramMask(LowByte) : 0,
    fuseBitsBox));
  fuseBitsLayout->addWidget(createBitColumn(
    QStringLiteral("Extended"), ExtendedByte, m_device.fuseCount() >= 3,
    m_device.fuseCount() >= 3 ? fuseReadMask(ExtendedByte) : 0,
    m_device.fuseCount() >= 3 ? fuseProgramMask(ExtendedByte) : 0,
    fuseBitsBox));
  bitsRow->addWidget(fuseBitsBox);

  auto* lockBitsBox = new QGroupBox(QStringLiteral("Lock Bits"), this);
  lockBitsBox->setFixedWidth(kLockSectionWidth);
  auto* lockBitsLayout = new QHBoxLayout(lockBitsBox);
  lockBitsLayout->setContentsMargins(kSectionInnerMargin, 7,
                                     kSectionInnerMargin, 5);
  lockBitsLayout->addWidget(createBitColumn(
    QStringLiteral("Lock"), LockByte, lockSupported,
    lockSupported ? 0xFF : 0, m_device.lockProgramMask, lockBitsBox));
  bitsRow->addWidget(lockBitsBox);
  bitsRow->addStretch();
  mainLayout->addLayout(bitsRow);

  auto* decodedBox = new QGroupBox(QStringLiteral("Decoded Settings"), this);
  auto* decodedLayout = new QVBoxLayout(decodedBox);
  decodedLayout->setContentsMargins(5, 7, 5, 5);
  m_optionsTree = new QTreeWidget(decodedBox);
  m_optionsTree->setColumnCount(1);
  m_optionsTree->setHeaderHidden(true);
  m_optionsTree->setRootIsDecorated(true);
  m_optionsTree->setAlternatingRowColors(true);
  m_optionsTree->setUniformRowHeights(true);
  m_optionsTree->setIndentation(14);
  m_optionsTree->header()->setStretchLastSection(true);
  decodedLayout->addWidget(m_optionsTree, 1);
  mainLayout->addWidget(decodedBox, 1);

  m_statusLabel = new QLabel(QStringLiteral("Ready"), this);
  m_statusLabel->setMinimumHeight(18);
  m_statusLabel->setStyleSheet(QStringLiteral(
    "color: #39546a; font-weight: 600; padding-left: 2px;"));
  mainLayout->addWidget(m_statusLabel);

  buildDecodedOptions();
  refreshUi();

  connect(m_optionsTree, &QTreeWidget::itemChanged,
          this, [this](QTreeWidgetItem* item, int) {
    if (m_updating || !item || !item->parent()) return;
    applyOption(item, item->checkState(0) == Qt::Checked);
  });
  connect(readFusesButton, &QPushButton::clicked,
          this, &FuseLockDialog::readRequested);
  connect(readLockButton, &QPushButton::clicked,
          this, &FuseLockDialog::readRequested);
  connect(defaultButton, &QPushButton::clicked, this, [this] {
    for (int i = 0; i < 3; ++i) {
      if (i < m_device.fuseCount()) {
        m_values[i] = m_device.fuseFactoryValues.value(i, 0xFF) & 0xFF;
      }
    }
    setStatus(QStringLiteral("Factory fuse defaults loaded."), true);
    refreshUi();
  });
  connect(m_writeFusesButton, &QPushButton::clicked, this, [this] {
    if (!parseRawEdits()) return;
    Q_EMIT writeFusesRequested(this->fuseValues());
  });
  connect(m_writeLockButton, &QPushButton::clicked, this, [this] {
    if (!parseRawEdits()) return;
    Q_EMIT writeLockRequested(this->lockValue());
  });

  DisplayLanguage::translateWidgetTree(this, QStringLiteral("en"));

  setFocusPolicy(Qt::StrongFocus);
  QTimer::singleShot(0, this, [this] {
    for (QLineEdit* edit : m_valueEdits) {
      if (!edit) continue;
      edit->deselect();
      edit->clearFocus();
    }
    setFocus(Qt::OtherFocusReason);
  });
}

QVector<int> FuseLockDialog::fuseValues() const {
  QVector<int> result;
  for (int i = 0; i < m_device.fuseCount(); ++i) {
    result.append(m_values.value(i, 0xFF) & 0xFF);
  }
  return result;
}

int FuseLockDialog::lockValue() const {
  return m_values.value(LockByte, 0xFF) & 0xFF;
}

void FuseLockDialog::setReadValues(const QVector<int>& values, int lockValue) {
  for (int i = 0; i < std::min(3, static_cast<int>(values.size())); ++i) {
    m_values[i] = values.at(i) & 0xFF;
  }
  if (lockValue >= 0) m_values[LockByte] = lockValue & 0xFF;
  refreshUi();
  setStatus(QStringLiteral("Fuse and lock values read."), true);
}

void FuseLockDialog::setStatus(const QString& text, bool success) {
  m_statusLabel->setText(DisplayLanguage::text(text));
  m_statusLabel->setStyleSheet(success
    ? QStringLiteral("color: #157347; font-weight: 600; padding-left: 2px;")
    : QStringLiteral("color: #a11d33; font-weight: 600; padding-left: 2px;"));
}

void FuseLockDialog::focusLockSection() {
  if (!m_optionsTree || !m_lockRoot) return;
  m_optionsTree->scrollToItem(m_lockRoot, QAbstractItemView::PositionAtTop);
  m_optionsTree->setCurrentItem(m_lockRoot);
}

void FuseLockDialog::closeEvent(QCloseEvent* event) {
  if (!parseRawEdits()) {
    updateRawEdits();
  }
  QDialog::closeEvent(event);
}

QLineEdit* FuseLockDialog::createValueEdit(int byteIndex, bool supported,
                                           QWidget* parent) {
  auto* edit = new QLineEdit(supported ? byteText(m_values[byteIndex])
                                      : QStringLiteral("N/A"), parent);
  edit->setAlignment(Qt::AlignCenter);
  edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  edit->setFixedWidth(56);
  edit->setMaxLength(2);
  edit->setEnabled(supported);
  edit->setValidator(new QRegularExpressionValidator(
    QRegularExpression(QStringLiteral("^[0-9A-Fa-f]{0,2}$")), edit));
  if (!supported) {
    edit->setToolTip(QStringLiteral("This MCU does not implement this byte."));
  }
  connect(edit, &QLineEdit::textEdited, this,
          [this, byteIndex, edit](const QString& text) {
    if (m_updating || text.isEmpty()) return;
    bool ok = false;
    const int value = parseHexByte(text, ok);
    if (!ok) return;

    const QString upper = text.toUpper();
    if (upper != text) {
      const int cursor = edit->cursorPosition();
      const QSignalBlocker blocker(edit);
      edit->setText(upper);
      edit->setCursorPosition(std::min(cursor, static_cast<int>(upper.size())));
    }

    setByteValue(byteIndex, value);
    m_updating = true;
    updateBitControls();
    updateDecodedChecks();
    m_updating = false;
  });
  connect(edit, &QLineEdit::editingFinished, this, [this] {
    if (parseRawEdits()) refreshUi();
  });
  return edit;
}

QWidget* FuseLockDialog::createBitColumn(const QString& title, int byteIndex,
                                         bool supported, quint8 readableMask,
                                         quint8 writableMask, QWidget* parent) {
  auto* column = new QWidget(parent);
  column->setFixedWidth(kByteColumnWidth);
  auto* layout = new QGridLayout(column);
  layout->setContentsMargins(2, 0, 2, 1);
  layout->setHorizontalSpacing(3);
  layout->setVerticalSpacing(1);

  auto* titleLabel = new QLabel(title, column);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleLabel->setStyleSheet(QStringLiteral("font-weight: 700;"));
  layout->addWidget(titleLabel, 0, 0, 1, 3);

  for (int bit = 7; bit >= 0; --bit) {
    const int row = 8 - bit;
    const quint8 mask = static_cast<quint8>(1u << bit);
    const QJsonObject metadata = bitMetadata(byteIndex, bit);
    const bool documented = !metadata.isEmpty();
    const bool readable = supported
      && (documented || (readableMask & mask) != 0);
    const bool hardwareWritable = supported && (writableMask & mask) != 0;
    const bool selectable = supported && hardwareWritable;

    auto* stateButton = new QPushButton(column);
    stateButton->setFixedSize(18, 18);
    stateButton->setFocusPolicy(Qt::NoFocus);
    stateButton->setProperty("readable", readable);
    stateButton->setProperty("writable", hardwareWritable);
    stateButton->setProperty("hardwareWritable", hardwareWritable);
    stateButton->setEnabled(selectable);
    stateButton->setToolTip(readable
      ? bitDescription(byteIndex, bit)
      : QStringLiteral("Reserved or unavailable bit. Its value is preserved."));
    m_bitButtons[byteIndex][bit] = stateButton;

    auto* bitNumber = new QLabel(QString::number(bit), column);
    bitNumber->setFixedWidth(10);
    bitNumber->setAlignment(Qt::AlignCenter);
    bitNumber->setStyleSheet(QStringLiteral("font-family: Consolas;"));

    QString name = readable ? bitName(byteIndex, bit)
                            : QStringLiteral("Reserved");
    auto* nameLabel = new QLabel(name, column);
    nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    nameLabel->setToolTip(stateButton->toolTip());
    nameLabel->setStyleSheet(QStringLiteral("font-family: Consolas;"));

    connect(stateButton, &QPushButton::clicked, this,
            [this, byteIndex, bit] {
      if (m_updating) return;
      int value = byteValue(byteIndex);
      value ^= (1 << bit);
      setByteValue(byteIndex, value);
      refreshUi();
    });

    layout->addWidget(stateButton, row, 0);
    layout->addWidget(bitNumber, row, 1);
    layout->addWidget(nameLabel, row, 2);
  }
  layout->setColumnStretch(2, 1);
  return column;
}

void FuseLockDialog::buildDecodedOptions() {
  m_optionsTree->clear();
  const QVector<int> fuseOrder{HighByte, LowByte, ExtendedByte};
  for (int byteIndex : fuseOrder) {
    const QJsonObject section = fuseMetadata(byteIndex);
    addOptionGroup(byteTitle(byteIndex), byteIndex,
                   section.value(QStringLiteral("options")).toArray());
  }
  const QJsonObject lock = m_metadata.value(QStringLiteral("lock")).toObject();
  m_lockRoot = new QTreeWidgetItem(m_optionsTree,
    QStringList{QStringLiteral("Lock Fuse")});
  m_lockRoot->setFlags(m_lockRoot->flags() & ~Qt::ItemIsUserCheckable);
  QFont rootFont = m_lockRoot->font(0);
  rootFont.setBold(true);
  m_lockRoot->setFont(0, rootFont);
  const QJsonArray options = lock.value(QStringLiteral("options")).toArray();
  for (const QJsonValue& value : options) {
    const QJsonObject option = value.toObject();
    auto* item = new QTreeWidgetItem(m_lockRoot,
      QStringList{normalizeOptionText(
        option.value(QStringLiteral("text")).toString())});
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(0, Qt::UserRole, LockByte);
    item->setData(0, Qt::UserRole + 1,
                  option.value(QStringLiteral("mask")).toInt());
    item->setData(0, Qt::UserRole + 2,
                  option.value(QStringLiteral("value")).toInt());
    item->setCheckState(0, Qt::Unchecked);
  }
  m_optionsTree->expandAll();
}

void FuseLockDialog::addOptionGroup(const QString& title, int byteIndex,
                                    const QJsonArray& options) {
  auto* root = new QTreeWidgetItem(m_optionsTree, QStringList{title});
  root->setFlags(root->flags() & ~Qt::ItemIsUserCheckable);
  QFont rootFont = root->font(0);
  rootFont.setBold(true);
  root->setFont(0, rootFont);
  if (byteIndex >= m_device.fuseCount()) {
    root->setDisabled(true);
    root->setText(0, title + QStringLiteral(" — Not Available"));
    return;
  }
  if (options.isEmpty()) {
    auto* item = new QTreeWidgetItem(root,
      QStringList{QStringLiteral("No decoded options available")});
    item->setDisabled(true);
    return;
  }
  for (const QJsonValue& value : options) {
    const QJsonObject option = value.toObject();
    auto* item = new QTreeWidgetItem(root,
      QStringList{normalizeOptionText(
        option.value(QStringLiteral("text")).toString())});
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(0, Qt::UserRole, byteIndex);
    item->setData(0, Qt::UserRole + 1,
                  option.value(QStringLiteral("mask")).toInt());
    item->setData(0, Qt::UserRole + 2,
                  option.value(QStringLiteral("value")).toInt());
    item->setCheckState(0, Qt::Unchecked);
  }
}

QJsonObject FuseLockDialog::fuseMetadata(int byteIndex) const {
  const QJsonArray fuses = m_metadata.value(QStringLiteral("fuses")).toArray();
  for (const QJsonValue& value : fuses) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("byteIndex")).toInt(-1) == byteIndex) {
      return object;
    }
  }
  return {};
}

QJsonObject FuseLockDialog::bitMetadata(int byteIndex, int bit) const {
  QJsonArray bits;
  if (byteIndex == LockByte) {
    bits = m_metadata.value(QStringLiteral("lock")).toObject()
      .value(QStringLiteral("bits")).toArray();
  } else {
    bits = fuseMetadata(byteIndex).value(QStringLiteral("bits")).toArray();
  }
  for (const QJsonValue& value : bits) {
    const QJsonObject object = value.toObject();
    if (object.value(QStringLiteral("bit")).toInt(-1) == bit) return object;
  }
  return {};
}

QString FuseLockDialog::bitName(int byteIndex, int bit) const {
  const QJsonObject metadata = bitMetadata(byteIndex, bit);
  QString name = metadata.value(QStringLiteral("name")).toString();
  if (name.compare(QStringLiteral("WTDON"), Qt::CaseInsensitive) == 0) {
    name = QStringLiteral("WDTON");
  }
  if (!name.isEmpty()) return name;
  return DisplayLanguage::text(QStringLiteral("Bit%1")).arg(bit);
}

QString FuseLockDialog::bitDescription(int byteIndex, int bit) const {
  return bitMetadata(byteIndex, bit)
    .value(QStringLiteral("description")).toString();
}

bool FuseLockDialog::parseRawEdits() {
  for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
    QLineEdit* edit = m_valueEdits.value(byteIndex);
    if (!edit || !edit->isEnabled()) continue;
    bool ok = false;
    const int value = parseHexByte(edit->text(), ok);
    if (!ok) {
      setStatus(QStringLiteral("%1 must be one hexadecimal byte.")
        .arg(byteTitle(byteIndex)), false);
      edit->setFocus();
      edit->selectAll();
      return false;
    }
    m_values[byteIndex] = value;
  }
  return true;
}

void FuseLockDialog::setByteValue(int byteIndex, int value) {
  if (byteIndex < 0 || byteIndex >= m_values.size()) return;
  m_values[byteIndex] = value & 0xFF;
}

int FuseLockDialog::byteValue(int byteIndex) const {
  return m_values.value(byteIndex, 0xFF) & 0xFF;
}

void FuseLockDialog::updateRawEdits() {
  for (int byteIndex = 0; byteIndex < 4; ++byteIndex) {
    QLineEdit* edit = m_valueEdits.value(byteIndex);
    if (!edit || !edit->isEnabled()) continue;
    const QSignalBlocker blocker(edit);
    edit->setText(byteText(byteValue(byteIndex)));
  }
}

void FuseLockDialog::updateBitControls() {
  for (int byteIndex = 0; byteIndex < m_bitButtons.size(); ++byteIndex) {
    for (int bit = 0; bit < 8; ++bit) {
      QPushButton* button = m_bitButtons[byteIndex].value(bit);
      if (!button) continue;
      const bool rawOne = (byteValue(byteIndex) & (1 << bit)) != 0;
      const bool readable = button->property("readable").toBool();
      const bool writable = button->property("writable").toBool();
      button->setText(rawOne ? QStringLiteral("1") : QStringLiteral("0"));
      if (!readable || !writable) {
        button->setStyleSheet(QStringLiteral(
          "QPushButton { background: #777d83; color: white; border: 1px solid #5f6469;"
          " border-radius: 2px; font-weight: 700; padding: 0; }"));
      } else if (rawOne) {
        button->setStyleSheet(QStringLiteral(
          "QPushButton { background: #159947; color: white; border: 1px solid #087431;"
          " border-radius: 2px; font-weight: 700; padding: 0; }"
          "QPushButton:hover { background: #20ad56; }"
          "QPushButton:disabled { background: #159947; color: white; border-color: #087431; }"));
      } else {
        button->setStyleSheet(QStringLiteral(
          "QPushButton { background: #d33b3b; color: white; border: 1px solid #a92121;"
          " border-radius: 2px; font-weight: 700; padding: 0; }"
          "QPushButton:hover { background: #e24a4a; }"
          "QPushButton:disabled { background: #d33b3b; color: white; border-color: #a92121; }"));
      }
      button->setEnabled(writable);
    }
  }
}

void FuseLockDialog::updateDecodedChecks() {
  if (!m_optionsTree) return;
  for (QTreeWidgetItemIterator iterator(m_optionsTree); *iterator; ++iterator) {
    QTreeWidgetItem* item = *iterator;
    if (!item->parent()) continue;
    const QVariant byteData = item->data(0, Qt::UserRole);
    if (!byteData.isValid()) continue;
    const int byteIndex = byteData.toInt();
    const int mask = item->data(0, Qt::UserRole + 1).toInt();
    const int value = item->data(0, Qt::UserRole + 2).toInt();
    item->setCheckState(0,
      (byteValue(byteIndex) & mask) == (value & mask)
        ? Qt::Checked : Qt::Unchecked);
  }
}

void FuseLockDialog::refreshUi() {
  m_updating = true;
  updateRawEdits();
  updateBitControls();
  updateDecodedChecks();
  m_updating = false;
}

void FuseLockDialog::applyOption(QTreeWidgetItem* item, bool checked) {
  if (!item) return;
  const QVariant byteData = item->data(0, Qt::UserRole);
  if (!byteData.isValid()) return;
  bool byteIndexOk = false;
  const int byteIndex = byteData.toInt(&byteIndexOk);
  if (!byteIndexOk) return;
  const int mask = item->data(0, Qt::UserRole + 1).toInt();
  const int value = item->data(0, Qt::UserRole + 2).toInt();
  if (byteIndex < 0 || byteIndex > LockByte || mask == 0) return;

  const bool singleBit = (mask & (mask - 1)) == 0;
  if (!checked && !singleBit) {
    refreshUi();
    return;
  }

  int selectedValue = value & mask;
  if (!checked) {
    selectedValue = (~value) & mask;
  }
  setByteValue(byteIndex,
    (byteValue(byteIndex) & ~mask) | selectedValue);
  refreshUi();
}

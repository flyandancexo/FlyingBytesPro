// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/UsbAspDriverDialog.h"

#include "gui/DisplayLanguage.h"
#include <QByteArray>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QHBoxLayout>
#include <QFrame>
#include <QFont>
#include <QIcon>
#include <QLabel>
#include <QMetaObject>
#include <QPointer>
#include <QProgressBar>
#include <QPixmap>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>
#include <QVBoxLayout>

#include <functional>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <shellapi.h>
#include <setupapi.h>
#include <winver.h>
extern "C" {
#include <libwdi.h>
}
#endif

namespace {

enum class InstallFailure {
  None,
  InvalidIdentifier,
  UnsupportedDriver,
  Enumeration,
  DeviceMissing,
  AmbiguousDevice,
  PackageFolder,
  Prepare,
  Install,
  ElevationCancelled,
  ElevationLaunch,
  HelperFailure,
};

struct DirectInstallResult {
  bool success = false;
  InstallFailure failure = InstallFailure::None;
  int nativeCode = 0;
  QString detail;
};

constexpr int kHelperInvalidArguments = 10;
constexpr int kHelperUnsupportedDriver = 11;
constexpr int kHelperDeviceMissing = 12;
constexpr int kHelperAmbiguousDevice = 13;
constexpr int kHelperPackageFolder = 14;
constexpr int kHelperEnumerationBase = 1000;
constexpr int kHelperPrepareBase = 2000;
constexpr int kHelperInstallBase = 3000;

QString driverDisplay(const QString& driver) {
  if (driver.isEmpty()) return DisplayLanguage::text(QStringLiteral("No active driver"));
  if (driver.compare(QStringLiteral("WinUSB"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("WinUSB");
  }
  return driver;
}

#if defined(Q_OS_WIN)
QString setupApiPropertyString(HDEVINFO set, SP_DEVINFO_DATA* device, DWORD property) {
  DWORD type = 0;
  DWORD required = 0;
  SetupDiGetDeviceRegistryPropertyW(
    set, device, property, &type, nullptr, 0, &required);
  if (required == 0) return {};

  QByteArray buffer(static_cast<int>(required + sizeof(wchar_t)), '\0');
  if (!SetupDiGetDeviceRegistryPropertyW(
        set, device, property, &type,
        reinterpret_cast<PBYTE>(buffer.data()),
        static_cast<DWORD>(buffer.size()), nullptr)) {
    return {};
  }

  const auto* value = reinterpret_cast<const wchar_t*>(buffer.constData());
  return QString::fromWCharArray(value).trimmed();
}

QStringList setupApiMultiString(HDEVINFO set, SP_DEVINFO_DATA* device, DWORD property) {
  DWORD type = 0;
  DWORD required = 0;
  SetupDiGetDeviceRegistryPropertyW(
    set, device, property, &type, nullptr, 0, &required);
  if (required == 0) return {};

  QByteArray buffer(static_cast<int>(required + (2 * sizeof(wchar_t))), '\0');
  if (!SetupDiGetDeviceRegistryPropertyW(
        set, device, property, &type,
        reinterpret_cast<PBYTE>(buffer.data()),
        static_cast<DWORD>(buffer.size()), nullptr)) {
    return {};
  }

  QStringList result;
  const auto* current = reinterpret_cast<const wchar_t*>(buffer.constData());
  while (*current != L'\0') {
    const QString value = QString::fromWCharArray(current);
    result.append(value);
    current += value.size() + 1;
  }
  return result;
}

QString setupApiInstanceId(HDEVINFO set, SP_DEVINFO_DATA* device) {
  DWORD required = 0;
  SetupDiGetDeviceInstanceIdW(set, device, nullptr, 0, &required);
  if (required == 0) return {};
  QVector<wchar_t> buffer(static_cast<int>(required + 1), L'\0');
  if (!SetupDiGetDeviceInstanceIdW(
        set, device, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr)) {
    return {};
  }
  return QString::fromWCharArray(buffer.constData()).trimmed();
}

bool parseVidPid(const QString& value, unsigned short* vid, unsigned short* pid) {
  const QStringList parts = value.split(QLatin1Char(':'));
  if (parts.size() != 2) return false;
  bool vidOk = false;
  bool pidOk = false;
  const unsigned int parsedVid = parts.at(0).toUInt(&vidOk, 16);
  const unsigned int parsedPid = parts.at(1).toUInt(&pidOk, 16);
  if (!vidOk || !pidOk || parsedVid > 0xFFFFU || parsedPid > 0xFFFFU) return false;
  if (!((parsedVid == 0x16C0U && parsedPid == 0x05DCU) ||
        (parsedVid == 0x03EBU && parsedPid == 0xC7B4U))) {
    return false;
  }
  *vid = static_cast<unsigned short>(parsedVid);
  *pid = static_cast<unsigned short>(parsedPid);
  return true;
}

QString fixedFileVersion(const VS_FIXEDFILEINFO& info) {
  return QStringLiteral("%1.%2.%3.%4")
    .arg(HIWORD(info.dwFileVersionMS))
    .arg(LOWORD(info.dwFileVersionMS))
    .arg(HIWORD(info.dwFileVersionLS))
    .arg(LOWORD(info.dwFileVersionLS));
}

QString fileVersion(const QString& path) {
  const std::wstring nativePath = QDir::toNativeSeparators(path).toStdWString();
  DWORD ignored = 0;
  const DWORD size = GetFileVersionInfoSizeW(nativePath.c_str(), &ignored);
  if (size == 0) return {};
  QByteArray data(static_cast<int>(size), '\0');
  if (!GetFileVersionInfoW(nativePath.c_str(), 0, size, data.data())) return {};
  VS_FIXEDFILEINFO* fixed = nullptr;
  UINT fixedSize = 0;
  if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &fixedSize) ||
      fixed == nullptr || fixedSize < sizeof(VS_FIXEDFILEINFO)) {
    return {};
  }
  return fixedFileVersion(*fixed);
}

QString systemWinUsbVersion() {
  wchar_t systemDirectory[MAX_PATH + 1]{};
  const UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) return QStringLiteral("Windows system driver");
  const QString path = QDir(QString::fromWCharArray(systemDirectory))
    .filePath(QStringLiteral("drivers/winusb.sys"));
  const QString version = fileVersion(path);
  return version.isEmpty() ? QStringLiteral("Windows system driver") : version;
}

QString wdiErrorText(const QString& stage, int code) {
  const char* text = wdi_strerror(code);
  const QString detail = text != nullptr ? QString::fromUtf8(text) : QStringLiteral("Unknown error");
  return QStringLiteral("%1: %2 (%3)").arg(stage, detail).arg(code);
}

DirectInstallResult installDriverDirect(
    const QString& vidPid,
    const QString& selectedInstanceId,
    const std::function<void(const QString&)>& statusCallback) {
  unsigned short targetVid = 0;
  unsigned short targetPid = 0;
  if (!parseVidPid(vidPid, &targetVid, &targetPid)) {
    return {false, InstallFailure::InvalidIdentifier, 0,
      QStringLiteral("The selected USBasp identifier is invalid.")};
  }

  if (wdi_is_driver_supported(WDI_WINUSB, nullptr) == FALSE) {
    return {false, InstallFailure::UnsupportedDriver, 0,
      QStringLiteral("WinUSB support is not embedded in this FlyingBytesPro build.")};
  }

  wdi_options_create_list listOptions{};
  listOptions.list_all = TRUE;
  listOptions.list_hubs = FALSE;
  listOptions.trim_whitespaces = TRUE;

  wdi_device_info* list = nullptr;
  const int listResult = wdi_create_list(&list, &listOptions);
  if (listResult != WDI_SUCCESS) {
    return {false, InstallFailure::Enumeration, listResult,
      wdiErrorText(QStringLiteral("USB device enumeration failed"), listResult)};
  }

  wdi_device_info* selected = nullptr;
  QVector<wdi_device_info*> vidPidMatches;
  for (wdi_device_info* device = list; device != nullptr; device = device->next) {
    if (device->vid != targetVid || device->pid != targetPid) continue;
    vidPidMatches.append(device);
    if (!selectedInstanceId.isEmpty() && device->device_id != nullptr) {
      const QString deviceId = QString::fromUtf8(device->device_id).trimmed();
      if (deviceId.compare(selectedInstanceId, Qt::CaseInsensitive) == 0) {
        selected = device;
        break;
      }
    }
  }

  if (selected == nullptr && vidPidMatches.size() == 1) {
    selected = vidPidMatches.constFirst();
  }
  if (selected == nullptr) {
    wdi_destroy_list(list);
    if (vidPidMatches.isEmpty()) {
      return {false, InstallFailure::DeviceMissing, 0,
        QStringLiteral("The selected USBasp is no longer present. Click Refresh and try again.")};
    }
    return {false, InstallFailure::AmbiguousDevice, 0,
      QStringLiteral("Multiple matching USBasp devices are present and the selected instance could not be matched safely.")};
  }

  QString packageId = vidPid;
  packageId.replace(QLatin1Char(':'), QLatin1Char('_'));
  const QString packagePath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
    .filePath(QStringLiteral("FlyingBytesPro/usbasp-driver/winusb/%1").arg(packageId));

  QDir packageDir(packagePath);
  if (packageDir.exists() && !packageDir.removeRecursively()) {
    wdi_destroy_list(list);
    return {false, InstallFailure::PackageFolder, 0,
      QStringLiteral("Could not clear the temporary USBasp driver package folder.")};
  }
  if (!QDir().mkpath(packagePath)) {
    wdi_destroy_list(list);
    return {false, InstallFailure::PackageFolder, 0,
      QStringLiteral("Could not create the temporary USBasp driver package folder.")};
  }

  statusCallback(QStringLiteral("Preparing driver..."));

  QByteArray pathBytes = QDir::toNativeSeparators(packagePath).toUtf8();
  QByteArray infName = QByteArrayLiteral("FlyingBytesPro_USBasp_WinUSB.inf");
  QByteArray vendorName = QByteArrayLiteral("FlyingBytesPro");

  wdi_options_prepare_driver prepareOptions{};
  prepareOptions.driver_type = WDI_WINUSB;
  prepareOptions.vendor_name = vendorName.data();
  prepareOptions.device_guid = nullptr;
  prepareOptions.disable_cat = FALSE;
  prepareOptions.disable_signing = FALSE;
  prepareOptions.cert_subject = nullptr;
  prepareOptions.use_wcid_driver = FALSE;
  prepareOptions.external_inf = FALSE;

  const int prepareResult = wdi_prepare_driver(
    selected, pathBytes.data(), infName.data(), &prepareOptions);
  if (prepareResult != WDI_SUCCESS) {
    wdi_destroy_list(list);
    return {false, InstallFailure::Prepare, prepareResult,
      wdiErrorText(QStringLiteral("Driver preparation failed"), prepareResult)};
  }

  statusCallback(QStringLiteral("Installing WinUSB..."));

  wdi_options_install_driver installOptions{};
  installOptions.hWnd = nullptr;
  installOptions.install_filter_driver = FALSE;
  installOptions.pending_install_timeout = 30000;

  const int installResult = wdi_install_driver(
    selected, pathBytes.data(), infName.data(), &installOptions);
  wdi_destroy_list(list);
  if (installResult != WDI_SUCCESS) {
    return {false, InstallFailure::Install, installResult,
      wdiErrorText(QStringLiteral("Driver installation failed"), installResult)};
  }

  return {true, InstallFailure::None, 0, QStringLiteral("Driver installed successfully.")};
}

bool processIsElevated() {
  HANDLE token = nullptr;
  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
  TOKEN_ELEVATION elevation{};
  DWORD returned = 0;
  const BOOL ok = GetTokenInformation(
    token, TokenElevation, &elevation, sizeof(elevation), &returned);
  CloseHandle(token);
  return ok && elevation.TokenIsElevated != 0;
}

int encodedHelperExit(const DirectInstallResult& result) {
  if (result.success) return 0;
  switch (result.failure) {
    case InstallFailure::InvalidIdentifier: return kHelperInvalidArguments;
    case InstallFailure::UnsupportedDriver: return kHelperUnsupportedDriver;
    case InstallFailure::DeviceMissing: return kHelperDeviceMissing;
    case InstallFailure::AmbiguousDevice: return kHelperAmbiguousDevice;
    case InstallFailure::PackageFolder: return kHelperPackageFolder;
    case InstallFailure::Enumeration: return kHelperEnumerationBase + qAbs(result.nativeCode);
    case InstallFailure::Prepare: return kHelperPrepareBase + qAbs(result.nativeCode);
    case InstallFailure::Install: return kHelperInstallBase + qAbs(result.nativeCode);
    default: return kHelperInvalidArguments;
  }
}

QString win32ErrorText(DWORD code) {
  wchar_t* message = nullptr;
  const DWORD length = FormatMessageW(
    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
    nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
  QString result;
  if (length != 0 && message != nullptr) {
    result = QString::fromWCharArray(message, static_cast<int>(length)).trimmed();
  }
  if (message != nullptr) LocalFree(message);
  return result.isEmpty() ? QStringLiteral("Windows error %1").arg(code) : result;
}

DirectInstallResult resultFromHelperExit(DWORD exitCode) {
  if (exitCode == 0) {
    return {true, InstallFailure::None, 0, QStringLiteral("Driver installed successfully.")};
  }
  if (exitCode == kHelperInvalidArguments) {
    return {false, InstallFailure::InvalidIdentifier, 0,
      QStringLiteral("Driver installer received invalid arguments.")};
  }
  if (exitCode == kHelperUnsupportedDriver) {
    return {false, InstallFailure::UnsupportedDriver, 0,
      QStringLiteral("WinUSB support is not embedded in this FlyingBytesPro build.")};
  }
  if (exitCode == kHelperDeviceMissing) {
    return {false, InstallFailure::DeviceMissing, 0,
      QStringLiteral("The selected USBasp is no longer present. Click Refresh and try again.")};
  }
  if (exitCode == kHelperAmbiguousDevice) {
    return {false, InstallFailure::AmbiguousDevice, 0,
      QStringLiteral("Multiple matching USBasp devices are present and the selected instance could not be matched safely.")};
  }
  if (exitCode == kHelperPackageFolder) {
    return {false, InstallFailure::PackageFolder, 0,
      QStringLiteral("Could not create the temporary driver package folder.")};
  }

  int wdiCode = 0;
  QString stage;
  if (exitCode >= kHelperInstallBase && exitCode < kHelperInstallBase + 1000) {
    wdiCode = -static_cast<int>(exitCode - kHelperInstallBase);
    stage = QStringLiteral("Driver installation failed");
  } else if (exitCode >= kHelperPrepareBase && exitCode < kHelperPrepareBase + 1000) {
    wdiCode = -static_cast<int>(exitCode - kHelperPrepareBase);
    stage = QStringLiteral("Driver preparation failed");
  } else if (exitCode >= kHelperEnumerationBase && exitCode < kHelperEnumerationBase + 1000) {
    wdiCode = -static_cast<int>(exitCode - kHelperEnumerationBase);
    stage = QStringLiteral("USB device enumeration failed");
  } else {
    return {false, InstallFailure::HelperFailure, static_cast<int>(exitCode),
      QStringLiteral("Driver installer failed with exit code %1.").arg(exitCode)};
  }
  return {false, InstallFailure::HelperFailure, wdiCode, wdiErrorText(stage, wdiCode)};
}

DirectInstallResult installThroughElevation(
    const QString& vidPid,
    const QString& instanceId,
    const std::function<void(const QString&)>& statusCallback) {
  if (processIsElevated()) {
    return installDriverDirect(vidPid, instanceId, statusCallback);
  }

  statusCallback(QStringLiteral("Starting driver installation..."));

  const QByteArray encodedInstance = instanceId.toUtf8().toBase64(
    QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
  const QString parameters = QStringLiteral("--flyingbytespro-winusb-helper %1 %2")
    .arg(vidPid, QString::fromLatin1(encodedInstance));

  const std::wstring executable = QDir::toNativeSeparators(
    QCoreApplication::applicationFilePath()).toStdWString();
  const std::wstring nativeParameters = parameters.toStdWString();

  SHELLEXECUTEINFOW shellInfo{};
  shellInfo.cbSize = sizeof(shellInfo);
  shellInfo.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
  shellInfo.hwnd = nullptr;
  shellInfo.lpVerb = L"runas";
  shellInfo.lpFile = executable.c_str();
  shellInfo.lpParameters = nativeParameters.c_str();
  shellInfo.nShow = SW_HIDE;

  if (!ShellExecuteExW(&shellInfo)) {
    const DWORD error = GetLastError();
    if (error == ERROR_CANCELLED) {
      return {false, InstallFailure::ElevationCancelled, static_cast<int>(error),
        QStringLiteral("WinUSB installation was cancelled.")};
    }
    return {false, InstallFailure::ElevationLaunch, static_cast<int>(error),
      QStringLiteral("Could not start driver installation: %1")
        .arg(win32ErrorText(error))};
  }

  statusCallback(QStringLiteral("Installing WinUSB..."));
  WaitForSingleObject(shellInfo.hProcess, INFINITE);
  DWORD exitCode = 1;
  if (!GetExitCodeProcess(shellInfo.hProcess, &exitCode)) {
    const DWORD error = GetLastError();
    CloseHandle(shellInfo.hProcess);
    return {false, InstallFailure::HelperFailure, static_cast<int>(error),
      QStringLiteral("Could not read driver installation result: %1")
        .arg(win32ErrorText(error))};
  }
  CloseHandle(shellInfo.hProcess);
  return resultFromHelperExit(exitCode);
}
#endif

} // namespace

int runUsbAspDriverInstallHelper(const QStringList& arguments) {
#if !defined(Q_OS_WIN)
  Q_UNUSED(arguments);
  return -1;
#else
  const int marker = arguments.indexOf(QStringLiteral("--flyingbytespro-winusb-helper"));
  if (marker < 0) return -1;
  if (marker + 2 >= arguments.size()) return kHelperInvalidArguments;

  const QString vidPid = arguments.at(marker + 1);
  const QByteArray encodedInstance = arguments.at(marker + 2).toLatin1();
  const QByteArray decodedInstance = QByteArray::fromBase64(
    encodedInstance, QByteArray::Base64UrlEncoding);
  const QString instanceId = QString::fromUtf8(decodedInstance);
  if (instanceId.isEmpty()) return kHelperInvalidArguments;

  const DirectInstallResult result = installDriverDirect(
    vidPid, instanceId, [](const QString&) {});
  return encodedHelperExit(result);
#endif
}

UsbAspDriverDialog::UsbAspDriverDialog(QWidget* parent)
  : QDialog(parent) {
  setWindowTitle(QStringLiteral("FlyingBytePro USBasp Driver Installation"));
  setWindowIcon(QIcon(QStringLiteral(":/icons/FD-Logo.png")));
  setModal(true);
  setMinimumSize(520, 330);
  resize(540, 350);

  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(18, 16, 18, 16);
  layout->setSpacing(12);

  auto* headerRow = new QHBoxLayout;
  headerRow->setSpacing(12);

  auto* logoLabel = new QLabel(this);
  logoLabel->setFixedSize(70, 70);
  logoLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  const QPixmap logo(QStringLiteral(":/icons/FD-Logo.png"));
  logoLabel->setPixmap(logo.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  headerRow->addWidget(logoLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);

  auto* titleColumn = new QVBoxLayout;
  titleColumn->setSpacing(2);
  titleColumn->addStretch();
  auto* titleLabel = new QLabel(QStringLiteral("USBasp Driver Installation"), this);
  QFont titleFont = titleLabel->font();
  titleFont.setPointSize(titleFont.pointSize() + 4);
  titleFont.setBold(true);
  titleLabel->setFont(titleFont);
  titleLabel->setAlignment(Qt::AlignCenter);
  titleColumn->addWidget(titleLabel);
  auto* subtitleLabel = new QLabel(QStringLiteral("Microsoft WinUSB"), this);
  subtitleLabel->setAlignment(Qt::AlignCenter);
  titleColumn->addWidget(subtitleLabel);
  titleColumn->addStretch();
  headerRow->addLayout(titleColumn, 1);

  auto* usbOnLabel = new QLabel(this);
  usbOnLabel->setFixedSize(70, 70);
  usbOnLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  const QPixmap usbOn(QStringLiteral(":/icons/usbasp_on.png"));
  usbOnLabel->setPixmap(usbOn.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
  headerRow->addWidget(usbOnLabel, 0, Qt::AlignRight | Qt::AlignVCenter);

  layout->addLayout(headerRow);

  auto* devicePanel = new QFrame(this);
  devicePanel->setFrameShape(QFrame::StyledPanel);
  devicePanel->setFrameShadow(QFrame::Plain);
  auto* devicePanelLayout = new QVBoxLayout(devicePanel);
  devicePanelLayout->setContentsMargins(14, 14, 14, 14);
  devicePanelLayout->setSpacing(10);

  auto* deviceRow = new QHBoxLayout;
  deviceRow->setSpacing(10);
  deviceRow->addWidget(new QLabel(QStringLiteral("USBasp Device:"), devicePanel));
  m_deviceCombo = new QComboBox(devicePanel);
  m_deviceCombo->setMinimumWidth(280);
  deviceRow->addWidget(m_deviceCombo, 1);
  devicePanelLayout->addLayout(deviceRow);

#if defined(Q_OS_WIN)
  const QString winUsbVersion = systemWinUsbVersion();
#else
  const QString winUsbVersion = QStringLiteral("Windows system driver");
#endif
  m_versionNote = new QLabel(
    DisplayLanguage::text(QStringLiteral("Microsoft WinUSB — %1")).arg(winUsbVersion), devicePanel);
  m_versionNote->setWordWrap(true);
  devicePanelLayout->addWidget(m_versionNote);

  m_deviceStatus = new QLabel(devicePanel);
  m_deviceStatus->setWordWrap(true);
  m_deviceStatus->setMinimumHeight(36);
  devicePanelLayout->addWidget(m_deviceStatus);

  m_installProgress = new QProgressBar(devicePanel);
  m_installProgress->setRange(0, 0);
  m_installProgress->setVisible(false);
  devicePanelLayout->addWidget(m_installProgress);

  layout->addWidget(devicePanel);
  layout->addStretch();

  auto* buttonRow = new QHBoxLayout;
  buttonRow->setSpacing(8);
  m_installButton = new QPushButton(QStringLiteral("Install Driver"), this);
  m_installButton->setMinimumWidth(120);
  m_refreshButton = new QPushButton(QStringLiteral("Refresh"), this);
  m_closeButton = new QPushButton(QStringLiteral("Close"), this);
  buttonRow->addWidget(m_installButton);
  buttonRow->addWidget(m_refreshButton);
  buttonRow->addStretch();
  buttonRow->addWidget(m_closeButton);
  layout->addLayout(buttonRow);

  connect(m_refreshButton, &QPushButton::clicked, this, [this] { refreshDevices(); });
  connect(m_installButton, &QPushButton::clicked, this, [this] { installOrRepairWinUsb(); });
  connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
  connect(m_deviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
    if (!m_installBusy) updateDeviceStatus();
  });

  DisplayLanguage::translateWidgetTree(this);
  refreshDevices();
}

QVector<UsbAspDriverDialog::DeviceInfo> UsbAspDriverDialog::enumerateUsbAspDevices() const {
  QVector<DeviceInfo> result;
#if defined(Q_OS_WIN)
  HDEVINFO set = SetupDiGetClassDevsW(
    nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
  if (set == INVALID_HANDLE_VALUE) return result;

  for (DWORD index = 0;; ++index) {
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    if (!SetupDiEnumDeviceInfo(set, index, &device)) {
      if (GetLastError() == ERROR_NO_MORE_ITEMS) break;
      continue;
    }

    const QStringList hardwareIds = setupApiMultiString(set, &device, SPDRP_HARDWAREID);
    QString vidPid;
    for (const QString& id : hardwareIds) {
      const QString upper = id.toUpper();
      if (upper.contains(QStringLiteral("VID_16C0&PID_05DC"))) {
        vidPid = QStringLiteral("16C0:05DC");
        break;
      }
      if (upper.contains(QStringLiteral("VID_03EB&PID_C7B4"))) {
        vidPid = QStringLiteral("03EB:C7B4");
        break;
      }
    }
    if (vidPid.isEmpty()) continue;

    QString description = setupApiPropertyString(set, &device, SPDRP_FRIENDLYNAME);
    if (description.isEmpty()) {
      description = setupApiPropertyString(set, &device, SPDRP_DEVICEDESC);
    }
    if (description.isEmpty()) description = QStringLiteral("USBasp");

    result.append({
      description,
      vidPid,
      setupApiPropertyString(set, &device, SPDRP_SERVICE),
      setupApiInstanceId(set, &device)
    });
  }
  SetupDiDestroyDeviceInfoList(set);
#endif
  return result;
}

void UsbAspDriverDialog::refreshDevices() {
  if (m_installBusy) return;
  m_deviceCombo->clear();
  const QVector<DeviceInfo> devices = enumerateUsbAspDevices();
  for (const DeviceInfo& device : devices) {
    const QString text = QStringLiteral("%1 — %2 — %3")
      .arg(device.description, device.vidPid, driverDisplay(device.driver));
    m_deviceCombo->addItem(text, device.vidPid);
    const int row = m_deviceCombo->count() - 1;
    m_deviceCombo->setItemData(row, device.driver, Qt::UserRole + 1);
    m_deviceCombo->setItemData(row, device.instanceId, Qt::UserRole + 2);
  }

  const bool found = !devices.isEmpty();
  m_installButton->setEnabled(found);
  if (!found) {
#if defined(Q_OS_WIN)
    m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
      "No USBasp detected. Connect the programmer and click Refresh.")));
#else
    m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
      "USBasp driver installation is available on Windows only.")));
#endif
    return;
  }
  updateDeviceStatus();
}

void UsbAspDriverDialog::updateDeviceStatus() {
  if (m_deviceCombo->currentIndex() < 0) return;
  const QString currentDriver = m_deviceCombo->currentData(Qt::UserRole + 1).toString();
  if (currentDriver.compare(QStringLiteral("WinUSB"), Qt::CaseInsensitive) == 0) {
    m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
      "WinUSB is already active.")));
  } else if (currentDriver.isEmpty()) {
    m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
      "No compatible driver is active.")));
  } else {
    m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
      "Current driver: %1."))
      .arg(driverDisplay(currentDriver)));
  }
}

QString UsbAspDriverDialog::selectedVidPid() const {
  return m_deviceCombo->currentData().toString();
}

QString UsbAspDriverDialog::selectedInstanceId() const {
  return m_deviceCombo->currentData(Qt::UserRole + 2).toString();
}

void UsbAspDriverDialog::setInstallBusy(bool busy, const QString& status) {
  m_installBusy = busy;
  m_installButton->setEnabled(!busy && m_deviceCombo->count() > 0);
  m_refreshButton->setEnabled(!busy);
  m_closeButton->setEnabled(!busy);
  m_deviceCombo->setEnabled(!busy);
  m_installProgress->setVisible(busy);
  if (!status.isEmpty()) {
    m_deviceStatus->setText(DisplayLanguage::text(status));
  }
}

void UsbAspDriverDialog::installOrRepairWinUsb() {
#if !defined(Q_OS_WIN)
  m_deviceStatus->setText(DisplayLanguage::text(QStringLiteral(
    "USBasp driver installation is available on Windows only.")));
  return;
#else
  if (m_installBusy || m_deviceCombo->currentIndex() < 0) return;

  const QString vidPid = selectedVidPid();
  const QString instanceId = selectedInstanceId();
  setInstallBusy(true, QStringLiteral("Preparing driver..."));

  const QPointer<UsbAspDriverDialog> guard(this);
  QThread* worker = QThread::create([guard, vidPid, instanceId] {
    const auto statusCallback = [guard](const QString& sourceText) {
      if (!guard) return;
      QMetaObject::invokeMethod(guard.data(), [guard, sourceText] {
        if (!guard) return;
        guard->m_deviceStatus->setText(DisplayLanguage::text(sourceText));
      }, Qt::QueuedConnection);
    };

    const DirectInstallResult result = installThroughElevation(
      vidPid, instanceId, statusCallback);

    if (!guard) return;
    QMetaObject::invokeMethod(guard.data(), [guard, result] {
      if (!guard) return;
      guard->setInstallBusy(false);
      if (result.success) {
        guard->m_deviceStatus->setText(
          DisplayLanguage::text(QStringLiteral("Driver installed successfully. Refreshing USBasp status...")));
        guard->refreshDevices();
        const QString currentDriver = guard->m_deviceCombo->currentData(Qt::UserRole + 1).toString();
        if (currentDriver.compare(QStringLiteral("WinUSB"), Qt::CaseInsensitive) == 0) {
          guard->m_deviceStatus->setText(
            DisplayLanguage::text(QStringLiteral("Driver installed successfully.")));
        } else {
          guard->m_deviceStatus->setText(
            DisplayLanguage::text(QStringLiteral(
              "Driver installation completed. Reconnect the USBasp if Windows has not refreshed it yet.")));
        }
      } else {
        guard->m_deviceStatus->setText(
          DisplayLanguage::text(QStringLiteral("Driver installation failed: %1"))
            .arg(DisplayLanguage::text(result.detail)));
      }
    }, Qt::QueuedConnection);
  });
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
#endif
}

void UsbAspDriverDialog::closeEvent(QCloseEvent* event) {
  if (m_installBusy) {
    event->ignore();
    return;
  }
  QDialog::closeEvent(event);
}

void UsbAspDriverDialog::reject() {
  if (m_installBusy) return;
  QDialog::reject();
}

// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>

class QCloseEvent;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

// Returns -1 for a normal application launch. When the hidden WinUSB helper
// switch is present, performs the selected USBasp WinUSB installation and
// returns the process exit code for main().
int runUsbAspDriverInstallHelper(const QStringList& arguments);

class UsbAspDriverDialog final : public QDialog {
public:
  explicit UsbAspDriverDialog(QWidget* parent = nullptr);

protected:
  void closeEvent(QCloseEvent* event) override;
  void reject() override;

private:
  struct DeviceInfo {
    QString description;
    QString vidPid;
    QString driver;
    QString instanceId;
  };

  QVector<DeviceInfo> enumerateUsbAspDevices() const;
  void refreshDevices();
  void installOrRepairWinUsb();
  void setInstallBusy(bool busy, const QString& status = QString());
  void updateDeviceStatus();
  QString selectedVidPid() const;
  QString selectedInstanceId() const;

  QComboBox* m_deviceCombo = nullptr;
  QLabel* m_deviceStatus = nullptr;
  QLabel* m_versionNote = nullptr;
  QProgressBar* m_installProgress = nullptr;
  QPushButton* m_installButton = nullptr;
  QPushButton* m_refreshButton = nullptr;
  QPushButton* m_closeButton = nullptr;
  bool m_installBusy = false;
};

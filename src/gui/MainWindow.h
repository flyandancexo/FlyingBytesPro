// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "avr/AvrDatabase.h"
#include "firmware/FirmwareImage.h"
#include "gui/FuseLockDialog.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QVector>

class HexTableModel;
class QCloseEvent;
class QDialog;
class MemoryMeterButton;
class ProgrammerController;
class SckDial;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QSettings;
class QProgressBar;
class QPushButton;
class QTabWidget;
class QTableView;
class QTimer;
class QToolButton;
class QWidget;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(bool demoMode, QWidget* parent = nullptr);
  ~MainWindow() override;

protected:
  void closeEvent(QCloseEvent* event) override;

Q_SIGNALS:
  void requestProbe(int clockId, bool quiet);
  void requestDetect(int clockId);
  void requestReadFlash(AvrDevice device, int clockId, bool fullRead);
  void requestReadEeprom(AvrDevice device, int clockId);
  void requestWriteFlash(AvrDevice device, FirmwareImage image,
                         bool eraseFirst, bool verifyAfter, int clockId);
  void requestWriteEeprom(AvrDevice device, FirmwareImage image,
                          bool verifyAfter, int clockId);
  void requestVerifyFlash(AvrDevice device, FirmwareImage image, int clockId);
  void requestVerifyEeprom(AvrDevice device, FirmwareImage image, int clockId);
  void requestErase(AvrDevice device, int clockId);
  void requestBlankFlash(AvrDevice device, int clockId);
  void requestBlankEeprom(AvrDevice device, int clockId);
  void requestReadFuses(AvrDevice device, int clockId);
  void requestWriteFuses(AvrDevice device, QVector<int> values, int clockId);
  void requestWriteLock(AvrDevice device, int value, int clockId);

private Q_SLOTS:
  void deviceChanged(int index);
  void handleProbeFinished(bool success, const QString& message,
                           quint32 capabilities, bool quiet);
  void handleSignatureFinished(bool success, const QString& message,
                               const QByteArray& signature);
  void handleImageFinished(const QString& operation, bool success,
                           const QString& message, const FirmwareImage& image);
  void handleOperationFinished(const QString& operation, bool success,
                               const QString& message);
  void handleFusesFinished(bool success, const QString& message,
                           const QVector<int>& values, int lockValue);
  void appendLog(const QString& message);
  void setProgress(int percent);
  void autoProbeTick();

private:
  enum class MemoryActivity {
    None,
    Flash,
    Eeprom
  };

  enum class ProgressState {
    Idle,
    Flash,
    Eeprom,
    Error
  };

  enum class TaskStep {
    None,
    ConfirmSignature,
    EraseChip,
    PrewriteFuses,
    ProgramFlash,
    ProgramEeprom,
    VerifyFlash,
    VerifyEeprom,
    ProgramFinalFuses,
    ProgramLock
  };

  struct MemoryUi {
    QWidget* page = nullptr;
    HexTableModel* model = nullptr;
    QTableView* table = nullptr;
    QPushButton* clearAllButton = nullptr;
    QPushButton* clearSelectedButton = nullptr;
    QPushButton* zeroAllButton = nullptr;
    QPushButton* zeroSelectedButton = nullptr;
    QPushButton* undoButton = nullptr;
    QPushButton* redoButton = nullptr;
    QLabel* bootInfoLabel = nullptr;
  };

  void buildUi();
  QWidget* buildTopStrip(QWidget* parent);
  QWidget* createTaskPage(QWidget* parent);
  MemoryUi createMemoryPage(const QString& title, bool flash, QWidget* parent);
  QWidget* createFullLogPage(QWidget* parent);
  QWidget* createSettingsPage(QWidget* parent);
  QWidget* createAboutPage(QWidget* parent);
  void connectController();
  void configureHexTable(QTableView* table, HexTableModel* model);
  void populateDevices();
  const AvrDevice* currentDevice() const;
  int currentClockId() const;
  bool confirmDiscardModifiedBuffers();
  void resetBuffersForDevice(const AvrDevice& device);
  void updateFuseUi(const AvrDevice& device);
  void loadDefaultFuseValues(const AvrDevice& device);
  void updateFuseButtonTexts();
  void updateBootloaderUi();
  void openFuseDialog(bool prewrite);
  void openLockDialog();
  void openFuseAndLockDialog(bool prewrite, bool focusLock);
  void applyDisplayLanguage(const QString& sourceCode);
  void updateMemoryIndicators();
  void startMemoryActivity(bool flash);
  void stopMemoryActivity();
  void updateMemoryActivityFrame();
  QPixmap memoryActivityFrame(bool flash, qreal lightOpacity) const;
  void updateUsbStatus(bool connected, const QString& text = {});
  void setBusy(bool busy, const QString& status = {},
               bool disableOperationWidgets = true);
  void startProgress(ProgressState state);
  void setProgressState(ProgressState state);
  void completeProgressResult(bool success);
  void showResult(bool success, const QString& message);
  void setMiniLog(const QString& message, bool success = true);
  void setTaskMiniLogLine(const QString& message, bool success);
  void restoreTaskSelections();
  void saveTaskSelections() const;
  void restoreFuseValuesForDevice(const AvrDevice& device);
  void saveFuseValuesForDeviceId(const QString& deviceId) const;

  void loadImage(bool flash);
  void saveImage(bool flash);
  void readMemory(bool flash);
  void writeMemory(bool flash, bool eraseFirst = false,
                   bool verifyAfter = true, bool askConfirmation = true);
  void verifyMemory(bool flash);
  void blankMemory(bool flash);

  void saveProject();
  void loadProject();
  QVector<int> fuseValuesFromEdits(const QVector<QLineEdit*>& edits,
                                   bool& ok) const;
  bool confirmMemoryWrite() const;
  bool ignoreMcuSignatureMatching() const;
  bool fullFlashRead() const;
  int lockValueFromUi(bool& ok) const;
  QString lastFileDirectory() const;
  void rememberFileDirectory(const QString& path) const;
  const AvrDevice* chooseSharedDevice(const QByteArray& signature,
                                      const QVector<const AvrDevice*>& matches);

  void startTaskSequence();
  void runNextTask();
  void completeTaskStep(bool success, const QString& message);
  void stopTaskSequence(const QString& message, bool success);
  QString taskName(TaskStep step) const;
  QString taskSummaryItem(TaskStep step, const QString& message) const;

  bool m_demoMode = false;
  bool m_busy = false;
  bool m_revertingDevice = false;
  bool m_usbConnected = false;
  bool m_autoProbeInProgress = false;
  bool m_deviceChangeIsDetection = false;
  bool m_signatureCheckOnly = false;
  int m_previousDeviceIndex = -1;
  AvrDatabase m_database;
  QThread m_workerThread;
  ProgrammerController* m_controller = nullptr;
  QTimer* m_autoProbeTimer = nullptr;
  QTimer* m_memoryActivityTimer = nullptr;
  QElapsedTimer m_memoryActivityElapsed;
  MemoryActivity m_memoryActivity = MemoryActivity::None;

  QComboBox* m_deviceCombo = nullptr;
  QLabel* m_signatureLabel = nullptr;
  QToolButton* m_usbaspButton = nullptr;
  QToolButton* m_detectButton = nullptr;
  QToolButton* m_checkMcuButton = nullptr;
  SckDial* m_sckDial = nullptr;

  MemoryMeterButton* m_flashMeter = nullptr;
  MemoryMeterButton* m_eepromMeter = nullptr;
  QToolButton* m_flashWriteButton = nullptr;
  QToolButton* m_eepromWriteButton = nullptr;
  QPixmap m_flashIconOn;
  QPixmap m_flashIconOff;
  QPixmap m_flashIconLight;
  QPixmap m_eepromIconOn;
  QPixmap m_eepromIconOff;
  QPixmap m_eepromIconLight;
  QPushButton* m_flashLoadButton = nullptr;
  QPushButton* m_flashSaveButton = nullptr;
  QPushButton* m_eepromLoadButton = nullptr;
  QPushButton* m_eepromSaveButton = nullptr;
  QPushButton* m_projectLoadButton = nullptr;
  QPushButton* m_projectSaveButton = nullptr;

  QTabWidget* m_tabs = nullptr;
  MemoryUi m_flashUi;
  MemoryUi m_eepromUi;
  QPlainTextEdit* m_log = nullptr;
  QLabel* m_aboutFeaturesLabel = nullptr;
  QComboBox* m_flashReadModeCombo = nullptr;
  QComboBox* m_languageCombo = nullptr;
  QPlainTextEdit* m_miniLog = nullptr;
  QProgressBar* m_progress = nullptr;
  ProgressState m_progressState = ProgressState::Idle;
  QPushButton* m_clearLogButton = nullptr;
  QPushButton* m_cancelButton = nullptr;

  QPointer<FuseLockDialog> m_activeFuseEditor;

  QVector<QCheckBox*> m_taskChecks;
  QPushButton* m_startTaskButton = nullptr;
  QPushButton* m_preFuseButton = nullptr;
  QPushButton* m_finalFuseButton = nullptr;
  QPushButton* m_lockFuseButton = nullptr;
  QVector<QLabel*> m_preFuseLabels;
  QVector<QLineEdit*> m_preFuseEdits;
  QVector<QLabel*> m_fuseLabels;
  QVector<QLineEdit*> m_fuseEdits;
  QLineEdit* m_lockEdit = nullptr;

  QCheckBox* m_confirmMemoryWritesCheck = nullptr;
  QCheckBox* m_verifyAfterWriteCheck = nullptr;
  QCheckBox* m_ignoreMcuSignatureMatchingCheck = nullptr;

  QVector<TaskStep> m_taskQueue;
  int m_taskQueueIndex = 0;
  TaskStep m_activeTask = TaskStep::None;
  bool m_taskSequenceActive = false;
  QElapsedTimer m_taskElapsedTimer;
  QStringList m_taskSummaryItems;
  QString m_taskMiniLogTimestamp;
  int m_taskMiniLogBlockNumber = -1;

  QVector<QWidget*> m_operationWidgets;
};

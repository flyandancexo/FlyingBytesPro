// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "avr/AvrDevice.h"
#include "firmware/FirmwareImage.h"
#include "gui/MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
  QApplication application(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("Flyandance"));
  QCoreApplication::setOrganizationDomain(QStringLiteral("flyandance.local"));
  QCoreApplication::setApplicationName(QStringLiteral("FlyingBytesPro"));
  QCoreApplication::setApplicationVersion(QStringLiteral("V3.2.20"));
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/app_256.png")));

  qRegisterMetaType<AvrDevice>();
  qRegisterMetaType<FirmwareImage>();
  qRegisterMetaType<QVector<int>>();

  QCommandLineParser parser;
  parser.setApplicationDescription(
    QStringLiteral("FlyingBytesPro — USBasp-only AVR programmer and editable memory tool."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption demoOption(
    QStringList{QStringLiteral("d"), QStringLiteral("demo")},
    QStringLiteral("Run without hardware using a simulated target."));
  const QCommandLineOption deploymentCheckOption(
    QStringLiteral("deployment-check"),
    QStringLiteral("Initialize Qt and exit immediately for portable-package validation."));
  parser.addOption(demoOption);
  parser.addOption(deploymentCheckOption);
  parser.process(application);

  if (parser.isSet(deploymentCheckOption)) {
    return 0;
  }

  MainWindow window(parser.isSet(demoOption));
  window.show();
  return application.exec();
}

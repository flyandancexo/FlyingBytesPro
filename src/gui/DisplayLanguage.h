// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QVector>

class QWidget;

namespace DisplayLanguage {

struct LanguageInfo {
  QString code;
  QString nativeName;
};

QVector<LanguageInfo> availableLanguages();
QString currentCode();
void setCurrentCode(const QString& code);
QString text(const QString& englishText);
QString textFrom(const QString& sourceText, const QString& sourceCode);
void translateWidgetTree(QWidget* root, const QString& sourceCode = QStringLiteral("en"));

} // namespace DisplayLanguage

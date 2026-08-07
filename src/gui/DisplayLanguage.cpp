// Copyright (C) 2026 Flyandance JZ
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gui/DisplayLanguage.h"

#include <QAbstractButton>
#include <QByteArray>
#include <QComboBox>
#include <QFile>
#include <QGroupBox>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QProgressBar>
#include <QVariant>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QWidget>

#include <algorithm>

namespace {

struct TemplateEntry {
  QRegularExpression expression;
  QVector<int> placeholderNumbers;
  QString replacement;
  int literalCharacters = 0;
};

struct LanguagePack {
  QHash<QString, QString> forward;
  QHash<QString, QString> reverse;
  QVector<TemplateEntry> forwardTemplates;
  QVector<TemplateEntry> reverseTemplates;
};

QString g_currentCode = QStringLiteral("en");
QHash<QString, LanguagePack> g_packs;

QString normalizedCode(QString code) {
  code = code.trimmed();
  const QVector<DisplayLanguage::LanguageInfo> languages =
    DisplayLanguage::availableLanguages();
  for (const auto& language : languages) {
    if (language.code.compare(code, Qt::CaseInsensitive) == 0) {
      return language.code;
    }
  }
  return QStringLiteral("en");
}

bool containsPlaceholder(const QString& text) {
  static const QRegularExpression placeholder(QStringLiteral("%[1-9]"));
  return placeholder.match(text).hasMatch();
}

TemplateEntry makeTemplate(const QString& pattern, const QString& replacement) {
  QString expression = QStringLiteral("^");
  QVector<int> placeholders;
  int literalStart = 0;
  for (int i = 0; i < pattern.size(); ++i) {
    if (pattern.at(i) != QLatin1Char('%') || i + 1 >= pattern.size()) continue;
    const QChar digit = pattern.at(i + 1);
    if (!digit.isDigit() || digit == QLatin1Char('0')) continue;
    if (i > literalStart) {
      expression += QRegularExpression::escape(pattern.mid(literalStart, i - literalStart));
    }
    expression += QStringLiteral("(.*?)");
    placeholders.append(digit.digitValue());
    ++i;
    literalStart = i + 1;
  }
  if (literalStart < pattern.size()) {
    expression += QRegularExpression::escape(pattern.mid(literalStart));
  }
  expression += QStringLiteral("$");
  const int literalCharacters = pattern.size() - placeholders.size() * 2;
  return {QRegularExpression(expression), placeholders, replacement, literalCharacters};
}

LanguagePack loadPack(const QString& code) {
  LanguagePack pack;
  if (code == QStringLiteral("en")) return pack;

  QFile file(QStringLiteral(":/languages/%1.json").arg(code));
  if (!file.open(QIODevice::ReadOnly)) return pack;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
  const QJsonObject strings = document.object().value(QStringLiteral("strings")).toObject();
  for (auto it = strings.constBegin(); it != strings.constEnd(); ++it) {
    const QString source = it.key();
    const QString target = it.value().toString();
    if (source.isEmpty() || target.isEmpty()) continue;
    pack.forward.insert(source, target);
    pack.reverse.insert(target, source);
    if (containsPlaceholder(source)) {
      pack.forwardTemplates.append(makeTemplate(source, target));
    }
    if (containsPlaceholder(target)) {
      pack.reverseTemplates.append(makeTemplate(target, source));
    }
  }
  const auto moreSpecific = [](const TemplateEntry& left, const TemplateEntry& right) {
    if (left.literalCharacters != right.literalCharacters) {
      return left.literalCharacters > right.literalCharacters;
    }
    return left.placeholderNumbers.size() < right.placeholderNumbers.size();
  };
  std::stable_sort(pack.forwardTemplates.begin(), pack.forwardTemplates.end(), moreSpecific);
  std::stable_sort(pack.reverseTemplates.begin(), pack.reverseTemplates.end(), moreSpecific);
  return pack;
}

const LanguagePack& packFor(const QString& code) {
  const QString normalized = normalizedCode(code);
  auto it = g_packs.find(normalized);
  if (it == g_packs.end()) {
    it = g_packs.insert(normalized, loadPack(normalized));
  }
  return it.value();
}

QString applyTemplates(const QString& input, const QVector<TemplateEntry>& templates) {
  for (const TemplateEntry& entry : templates) {
    const QRegularExpressionMatch match = entry.expression.match(input);
    if (!match.hasMatch()) continue;
    QString result = entry.replacement;
    for (int captureIndex = 0; captureIndex < entry.placeholderNumbers.size(); ++captureIndex) {
      const int placeholder = entry.placeholderNumbers.at(captureIndex);
      const QString token = QStringLiteral("%") + QString::number(placeholder);
      result.replace(token, match.captured(captureIndex + 1));
    }
    return result;
  }
  return input;
}

QString toEnglish(const QString& input, const QString& sourceCode) {
  const QString normalized = normalizedCode(sourceCode);
  if (normalized == QStringLiteral("en")) return input;
  const LanguagePack& pack = packFor(normalized);
  const auto exact = pack.reverse.constFind(input);
  if (exact != pack.reverse.constEnd()) return exact.value();
  return applyTemplates(input, pack.reverseTemplates);
}

QString fromEnglish(const QString& english, const QString& targetCode) {
  const QString normalized = normalizedCode(targetCode);
  if (normalized == QStringLiteral("en")) return english;
  const LanguagePack& pack = packFor(normalized);
  const auto exact = pack.forward.constFind(english);
  if (exact != pack.forward.constEnd()) return exact.value();
  return applyTemplates(english, pack.forwardTemplates);
}

QString translateStored(QObject* object, const char* role, const QString& current,
                        const QString& sourceCode) {
  if (!object) return DisplayLanguage::textFrom(current, sourceCode);
  const QByteArray canonicalProperty = QByteArray("_fbpLangCanonical_") + role;
  const QByteArray lastProperty = QByteArray("_fbpLangLast_") + role;
  const QVariant storedCanonical = object->property(canonicalProperty.constData());
  const QVariant storedLast = object->property(lastProperty.constData());

  QString canonical;
  if (!storedCanonical.isValid()
      || (storedLast.isValid() && current != storedLast.toString())) {
    canonical = toEnglish(current, sourceCode);
    object->setProperty(canonicalProperty.constData(), canonical);
  } else {
    canonical = storedCanonical.toString();
  }

  const QString translated = fromEnglish(canonical, g_currentCode);
  object->setProperty(lastProperty.constData(), translated);
  return translated;
}

void translateTreeItem(QTreeWidgetItem* item, const QString& sourceCode) {
  if (!item) return;
  for (int column = 0; column < item->columnCount(); ++column) {
    item->setText(column, DisplayLanguage::textFrom(item->text(column), sourceCode));
    item->setToolTip(column, DisplayLanguage::textFrom(item->toolTip(column), sourceCode));
  }
  for (int child = 0; child < item->childCount(); ++child) {
    translateTreeItem(item->child(child), sourceCode);
  }
}

void translateWidget(QWidget* widget, const QString& sourceCode) {
  if (!widget) return;
  widget->setWindowTitle(translateStored(
    widget, "windowTitle", widget->windowTitle(), sourceCode));
  widget->setToolTip(translateStored(
    widget, "toolTip", widget->toolTip(), sourceCode));
  widget->setStatusTip(translateStored(
    widget, "statusTip", widget->statusTip(), sourceCode));
  widget->setWhatsThis(translateStored(
    widget, "whatsThis", widget->whatsThis(), sourceCode));
  widget->setAccessibleName(translateStored(
    widget, "accessibleName", widget->accessibleName(), sourceCode));
  widget->setAccessibleDescription(translateStored(
    widget, "accessibleDescription", widget->accessibleDescription(), sourceCode));

  if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
    button->setText(translateStored(button, "buttonText", button->text(), sourceCode));
  }
  if (auto* label = qobject_cast<QLabel*>(widget)) {
    label->setText(translateStored(label, "labelText", label->text(), sourceCode));
  }
  if (auto* group = qobject_cast<QGroupBox*>(widget)) {
    group->setTitle(translateStored(group, "groupTitle", group->title(), sourceCode));
  }
  if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
    lineEdit->setPlaceholderText(translateStored(
      lineEdit, "placeholderText", lineEdit->placeholderText(), sourceCode));
  }
  if (auto* progress = qobject_cast<QProgressBar*>(widget)) {
    progress->setFormat(translateStored(
      progress, "progressFormat", progress->format(), sourceCode));
  }
  if (auto* combo = qobject_cast<QComboBox*>(widget)) {
    if (!combo->property("displayLanguageSelector").toBool()) {
      for (int i = 0; i < combo->count(); ++i) {
        combo->setItemText(i, DisplayLanguage::textFrom(combo->itemText(i), sourceCode));
      }
    }
  }
  if (auto* tabs = qobject_cast<QTabWidget*>(widget)) {
    for (int i = 0; i < tabs->count(); ++i) {
      tabs->setTabText(i, DisplayLanguage::textFrom(tabs->tabText(i), sourceCode));
      tabs->setTabToolTip(i, DisplayLanguage::textFrom(tabs->tabToolTip(i), sourceCode));
    }
  }
  if (auto* tree = qobject_cast<QTreeWidget*>(widget)) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
      translateTreeItem(tree->topLevelItem(i), sourceCode);
    }
  }
}

} // namespace

namespace DisplayLanguage {

QVector<LanguageInfo> availableLanguages() {
  return {
    {QStringLiteral("en"), QStringLiteral("English")},
    {QStringLiteral("zh-CN"), QStringLiteral("简体中文")},
    {QStringLiteral("es"), QStringLiteral("Español")},
    {QStringLiteral("ja"), QStringLiteral("日本語")},
    {QStringLiteral("de"), QStringLiteral("Deutsch")},
    {QStringLiteral("ko"), QStringLiteral("한국어")},
    {QStringLiteral("fr"), QStringLiteral("Français")},
    {QStringLiteral("vi"), QStringLiteral("Tiếng Việt")}
  };
}

QString currentCode() {
  return g_currentCode;
}

void setCurrentCode(const QString& code) {
  g_currentCode = normalizedCode(code);
  packFor(g_currentCode);
}

QString text(const QString& englishText) {
  return fromEnglish(englishText, g_currentCode);
}

QString textFrom(const QString& sourceText, const QString& sourceCode) {
  if (sourceText.isEmpty()) return sourceText;
  return fromEnglish(toEnglish(sourceText, sourceCode), g_currentCode);
}

void translateWidgetTree(QWidget* root, const QString& sourceCode) {
  if (!root) return;
  translateWidget(root, sourceCode);
  const QList<QWidget*> widgets = root->findChildren<QWidget*>(
    QString(), Qt::FindChildrenRecursively);
  for (QWidget* widget : widgets) {
    translateWidget(widget, sourceCode);
  }
}

} // namespace DisplayLanguage

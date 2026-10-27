#include "ui/editors/EditorPreferences.hpp"

#include "core/interfaces/ISettingsStore.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace mydb::ui {

namespace {

constexpr int kVersion = 1;

QStringList strings(const QJsonValue& value) {
    QStringList result;
    for (const auto& item : value.toArray()) {
        if (item.isString() && result.size() < EditorPreferences::kMaxHistory) {
            result.push_back(item.toString());
        }
    }
    return result;
}

} // namespace

EditorPreferences EditorPreferences::load(core::ISettingsStore& settings) {
    EditorPreferences preferences;
    const auto text = settings.read(kDocument);
    if (!text) {
        return preferences;
    }
    const QJsonObject root = QJsonDocument::fromJson(QByteArray::fromStdString(*text)).object();
    if (root.value(QStringLiteral("version")).toInt() != kVersion) {
        return preferences;
    }
    preferences.findHistory = strings(root.value(QStringLiteral("findHistory")));
    preferences.replaceHistory = strings(root.value(QStringLiteral("replaceHistory")));
    const int days = root.value(QStringLiteral("historyKeepDays")).toInt(kDefaultKeepDays);
    preferences.historyKeepDays = days > 0 ? days : kDefaultKeepDays;
    return preferences;
}

void EditorPreferences::save(core::ISettingsStore& settings) const {
    QJsonObject root;
    root.insert(QStringLiteral("version"), kVersion);
    root.insert(QStringLiteral("findHistory"), QJsonArray::fromStringList(findHistory));
    root.insert(QStringLiteral("replaceHistory"), QJsonArray::fromStringList(replaceHistory));
    root.insert(QStringLiteral("historyKeepDays"), historyKeepDays);
    settings.write(kDocument, QJsonDocument(root).toJson(QJsonDocument::Indented).toStdString());
}

void rememberText(QStringList& history, const QString& text) {
    if (text.isEmpty()) {
        return;
    }
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > EditorPreferences::kMaxHistory) {
        history.removeLast();
    }
}

} // namespace mydb::ui

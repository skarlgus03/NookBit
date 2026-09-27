#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <set>
#include <utility>

namespace nookbit {
namespace {

// 조건 종류를 JSON의 고정된 문자열로 변환하는 함수.
QString conditionName(WatchConditionType type) {
    switch (type) {
    case WatchConditionType::FileCount: return QStringLiteral("file_count");
    case WatchConditionType::TotalSizeMiB: return QStringLiteral("total_size_mib");
    case WatchConditionType::OldestAgeMinutes: return QStringLiteral("oldest_age_minutes");
    }
    return {};
}

// JSON의 조건 종류를 코어 값으로 복원하는 함수.
WatchConditionType parseCondition(const QString& name) {
    if (name == QStringLiteral("total_size_mib")) return WatchConditionType::TotalSizeMiB;
    if (name == QStringLiteral("oldest_age_minutes")) return WatchConditionType::OldestAgeMinutes;
    return WatchConditionType::FileCount;
}

// 저장 전 조건 중복과 숫자 범위를 확인하는 함수.
Result<void> validateSettings(const WatchSettings& settings) {
    if (!settings.folder.is_absolute() || settings.folder == settings.folder.root_path() ||
        settings.rescanIntervalMinutes < 1 || settings.rescanIntervalMinutes > 1440 ||
        settings.conditions.empty()) {
        return Result<void>::failure({
            "watch.settings_invalid", "A folder, interval, and at least one condition are required.",
        });
    }
    std::set<WatchConditionType> seen;
    for (const auto& condition : settings.conditions) {
        if (condition.threshold < 1 || condition.threshold > 1000000 ||
            !seen.insert(condition.type).second) {
            return Result<void>::failure({
                "watch.condition_invalid", "Watch conditions must be unique and between 1 and 1000000.",
            });
        }
    }
    std::set<std::string> patterns;
    for (const auto& pattern : settings.excludePatterns) {
        if (pattern.empty() || !patterns.insert(pattern).second) {
            return Result<void>::failure({
                "watch.exclude_invalid", "Exclude patterns must be non-empty and unique.",
            });
        }
    }
    return Result<void>::success();
}

} // namespace

// 설정 파일과 검증용 Schema 경로를 보관하는 생성자.
JsonWatchSettingsRepository::JsonWatchSettingsRepository(QString settingsPath, QString schemaPath)
    : settingsPath_(std::move(settingsPath)), schemaPath_(std::move(schemaPath)) {}

// 저장된 감시 폴더와 조건을 검증해 읽는 함수.
Result<WatchSettings> JsonWatchSettingsRepository::load() const {
    const auto loaded = loadValidatedJson(settingsPath_, schemaPath_);
    if (!loaded) return Result<WatchSettings>::failure(loaded.error());
    const auto& object = loaded.value();
    WatchSettings settings;
    settings.folder = std::filesystem::path(object.value(QStringLiteral("folder")).toString().toStdWString());
    settings.enabled = object.value(QStringLiteral("enabled")).toBool();
    settings.recursive = object.value(QStringLiteral("recursive")).toBool();
    settings.rescanIntervalMinutes = object.value(QStringLiteral("rescanIntervalMinutes")).toInt();
    settings.combination = object.value(QStringLiteral("combination")).toString() == QStringLiteral("all")
        ? WatchCombination::All : WatchCombination::Any;
    for (const auto& value : object.value(QStringLiteral("conditions")).toArray()) {
        const auto condition = value.toObject();
        settings.conditions.push_back({
            parseCondition(condition.value(QStringLiteral("type")).toString()),
            condition.value(QStringLiteral("threshold")).toInt(),
        });
    }
    for (const auto& pattern : object.value(QStringLiteral("excludePatterns")).toArray()) {
        settings.excludePatterns.push_back(pattern.toString().toStdString());
    }
    const auto checked = validateSettings(settings);
    if (!checked) return Result<WatchSettings>::failure(checked.error());
    return Result<WatchSettings>::success(std::move(settings));
}

// 감시 폴더와 조건을 검증해 한 번에 저장하는 함수.
Result<void> JsonWatchSettingsRepository::save(const WatchSettings& settings) const {
    const auto checked = validateSettings(settings);
    if (!checked) return checked;
    if (!QFileInfo(settingsPath_).isAbsolute() ||
        !QDir().mkpath(QFileInfo(settingsPath_).absolutePath())) {
        return Result<void>::failure({
            "watch.path_invalid", "Cannot prepare the watch settings location.",
        });
    }
    QJsonObject object;
    object.insert(QStringLiteral("schemaVersion"), 1);
    object.insert(QStringLiteral("folder"), QString::fromStdWString(settings.folder.wstring()));
    object.insert(QStringLiteral("enabled"), settings.enabled);
    object.insert(QStringLiteral("recursive"), settings.recursive);
    object.insert(QStringLiteral("rescanIntervalMinutes"), settings.rescanIntervalMinutes);
    object.insert(QStringLiteral("combination"),
                  settings.combination == WatchCombination::All ? QStringLiteral("all") : QStringLiteral("any"));
    QJsonArray conditions;
    for (const auto& condition : settings.conditions) {
        conditions.append(QJsonObject{
            {QStringLiteral("type"), conditionName(condition.type)},
            {QStringLiteral("threshold"), condition.threshold},
        });
    }
    object.insert(QStringLiteral("conditions"), conditions);
    QJsonArray patterns;
    for (const auto& pattern : settings.excludePatterns) {
        patterns.append(QString::fromStdString(pattern));
    }
    object.insert(QStringLiteral("excludePatterns"), patterns);
    const auto json = QJsonDocument(object).toJson(QJsonDocument::Indented);
    QSaveFile file(settingsPath_);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(json) != json.size() ||
        !file.commit()) {
        return Result<void>::failure({
            "watch.save_failed", "Cannot save watch settings atomically.",
        });
    }
    return Result<void>::success();
}

} // namespace nookbit

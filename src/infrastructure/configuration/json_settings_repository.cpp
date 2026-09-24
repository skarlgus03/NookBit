#include "infrastructure/configuration/json_settings_repository.hpp"
#include "infrastructure/configuration/json_document_loader.hpp"

#include <QJsonArray>
#include <QJsonObject>

#include <filesystem>
#include <utility>

namespace nookbit {
namespace {

// JSON 문자열을 Windows 파일 시스템 경로로 변환하는 함수.
std::filesystem::path toPath(const QString& text) {
    return std::filesystem::path(text.toStdWString());
}

} // namespace

// 설정 파일과 Schema 경로를 보관하는 생성자.
JsonSettingsRepository::JsonSettingsRepository(QString settingsPath, QString schemaPath)
    : settingsPath_(std::move(settingsPath)), schemaPath_(std::move(schemaPath)) {}

// 전체 JSON 구조를 검증하고 현재 코어가 사용하는 설정을 추출하는 함수.
Result<UserSettings> JsonSettingsRepository::load() const {
    auto loaded = loadValidatedJson(settingsPath_, schemaPath_);
    if (!loaded) return Result<UserSettings>::failure(loaded.error());

    const auto paths = loaded.value().value(QStringLiteral("paths")).toObject();
    const auto organization = loaded.value().value(QStringLiteral("organization")).toObject();
    UserSettings settings;
    settings.homeRoot = toPath(paths.value(QStringLiteral("homeRoot")).toString());
    if (!settings.homeRoot.is_absolute() ||
        settings.homeRoot == settings.homeRoot.root_path()) {
        return Result<UserSettings>::failure({
            "settings.home_root_invalid", "The organizer home must be an absolute folder below a drive root.",
        });
    }

    const auto policy = organization.value(QStringLiteral("collisionPolicy")).toString();
    if (policy == QStringLiteral("skip")) settings.defaultCollisionPolicy = CollisionPolicy::Skip;
    if (policy == QStringLiteral("abort")) settings.defaultCollisionPolicy = CollisionPolicy::Abort;
    if (organization.value(QStringLiteral("multiMatchPolicy")).toString() ==
        QStringLiteral("abort")) {
        settings.multiMatchPolicy = MultiMatchPolicy::Abort;
    }
    if (organization.value(QStringLiteral("unmatchedPolicy")).toString() ==
        QStringLiteral("abort")) {
        settings.unmatchedPolicy = UnmatchedPolicy::Abort;
    }

    bool recursionSet = false;
    for (const auto& pattern : paths.value(QStringLiteral("excludePatterns")).toArray()) {
        settings.excludePatterns.push_back(pattern.toString().toStdString());
    }
    for (const auto& value : paths.value(QStringLiteral("watchFolders")).toArray()) {
        const auto folder = value.toObject();
        if (!folder.value(QStringLiteral("enabled")).toBool()) continue;
        const auto root = toPath(folder.value(QStringLiteral("path")).toString());
        if (!root.is_absolute() || root == root.root_path()) {
            return Result<UserSettings>::failure({
                "settings.watch_root_invalid", "An enabled watch root must be an absolute folder below a drive root.",
            });
        }
        const bool recursive = folder.value(QStringLiteral("recursive")).toBool();
        // 현재 Scanner 계약은 모든 감시 폴더에 같은 재귀 옵션을 적용한다.
        if (recursionSet && settings.recursiveScan != recursive) {
            return Result<UserSettings>::failure({
                "settings.mixed_recursive_unsupported",
                "Enabled watch folders must use the same recursive setting.",
            });
        }
        settings.recursiveScan = recursive;
        recursionSet = true;
        settings.watchRoots.push_back(root);
    }

    return Result<UserSettings>::success(std::move(settings));
}

// 일부 설정만 담는 현재 코어 모델로 JSON 전체를 덮어쓰지 않도록 거부하는 함수.
Result<void> JsonSettingsRepository::save(const UserSettings&) {
    return Result<void>::failure({
        "settings.save_unavailable",
        "Settings editing will be enabled when all JSON fields have a writable model.",
    });
}

} // namespace nookbit

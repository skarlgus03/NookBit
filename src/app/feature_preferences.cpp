#include "app/feature_preferences.hpp"
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>

namespace nookbit {
namespace {
const auto selectionKey = QStringLiteral("modules/selection");
// 문서 손상 시 일부 선택만 임의로 적용하지 않고 오류를 반환하는 함수.
Result<FeatureSelection> invalidSelection() {
    return Result<FeatureSelection>::failure({"feature.invalid_preferences",
        "모듈 설정을 읽지 못해 기능을 시작하지 않았어요. 모듈 관리에서 선택한 뒤 저장해 주세요."});
}
}
// 사용자 기능 설정 전체를 읽고 버전과 실제 불리언 자료형을 검사하는 함수.
Result<FeatureSelection> FeaturePreferences::load() const {
    if (settings_.status() != QSettings::NoError) return invalidSelection();
    if (!settings_.contains(selectionKey)) return Result<FeatureSelection>::success({});
    const auto bytes = settings_.value(selectionKey).toByteArray();
    if (bytes.size() > 16384) return invalidSelection();
    const auto document = QJsonDocument::fromJson(bytes);
    if (!document.isObject()) return invalidSelection();
    const auto root = document.object();
    if (root.value("schemaVersion").toDouble(-1) != 1 || !root.value("enabled").isObject()) return invalidSelection();
    const auto enabled = root.value("enabled").toObject();
    for (const auto* id : {"file-organizer", "folder-watch", "notifications", "companion"})
        if (!enabled.value(id).isBool()) return invalidSelection();
    return Result<FeatureSelection>::success({enabled.value("file-organizer").toBool(), enabled.value("folder-watch").toBool(),
        enabled.value("notifications").toBool(), enabled.value("companion").toBool()});
}
// 단일 문서로 저장하여 선택 일부만 바뀌는 것을 피하고 쓰기 오류를 보고하는 함수.
Result<void> FeaturePreferences::save(FeatureSelection selection) {
    const bool existed = settings_.contains(selectionKey);
    const auto previous = settings_.value(selectionKey);
    const auto document = QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"enabled", QJsonObject{
        {"file-organizer", selection.organizer}, {"folder-watch", selection.watch},
        {"notifications", selection.notifications}, {"companion", selection.companion}}}});
    settings_.setValue(selectionKey, document.toJson(QJsonDocument::Compact));
    settings_.sync();
    if (settings_.status() == QSettings::NoError) return Result<void>::success();
    if (existed) settings_.setValue(selectionKey, previous); else settings_.remove(selectionKey);
    settings_.sync();
    return Result<void>::failure({"feature.save_failed", "모듈 선택을 저장하지 못했어요. 이전 선택을 유지해요."});
}
}

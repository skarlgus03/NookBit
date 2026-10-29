#include "app/feature_startup.hpp"
#include <QCoreApplication>

namespace nookbit {
// 감시의 필수 기능만 확인하고 선택적 표시 기능은 독립적으로 유지하는 함수.
FeatureStartupPlan resolveFeatureStartup(FeatureSelection requested) {
    FeatureStartupPlan plan{requested, requested, {}};
    if (requested.watch && !requested.organizer) {
        plan.active.watch = false;
        plan.messages.append(QCoreApplication::translate("FeatureStartup", "폴더 감시는 파일 정리 기능이 꺼져 있어 시작하지 않았어요."));
    }
    if (requested.feeding && !requested.companion) {
        plan.active.feeding = false;
        plan.messages.append(QCoreApplication::translate("FeatureStartup", "먹이 주기는 컴패니언 기능이 꺼져 있어 시작하지 않았어요."));
    }
    return plan;
}
// 반복 가능한 --disable-feature 옵션으로 이번 실행의 기능 선택만 만드는 함수.
Result<FeatureSelection> parseFeatureSelection(const QStringList& arguments, FeatureSelection selection) {
    const auto prefix = QStringLiteral("--disable-feature=");
    for (const auto& argument : arguments) {
        if (argument == QStringLiteral("--disable-feature"))
            return Result<FeatureSelection>::failure({"feature.missing_id", "Use --disable-feature=<feature-id>."});
        if (!argument.startsWith(prefix)) continue;
        const auto id = argument.mid(prefix.size());
        if (id == "all") selection = {false, false, false, false, false};
        else if (id == "file-organizer") selection.organizer = false;
        else if (id == "folder-watch") selection.watch = false;
        else if (id == "notifications") selection.notifications = false;
        else if (id == "companion") selection.companion = false;
        else if (id == "companion-feeding") selection.feeding = false;
        else return Result<FeatureSelection>::failure({"feature.unknown_id", "Unknown disabled feature: " + id.toStdString()});
    }
    return Result<FeatureSelection>::success(selection);
}
}

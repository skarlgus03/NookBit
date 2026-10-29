#include "presentation/feature_settings_controller.hpp"
#include <QVariantMap>

namespace nookbit {
// 현재 기능 객체와 무관한 설정 초안과 저장 콜백만 보관하는 생성자.
FeatureSettingsController::FeatureSettingsController(FeatureSelection saved, FeatureSelection active,
    bool sessionOverride, Save save, QString loadError, QObject* parent)
    : QObject(parent), saved_(saved), active_(active), draft_(saved), sessionOverride_(sessionOverride),
      invalidSaved_(!loadError.isEmpty()), save_(std::move(save)), status_(std::move(loadError)) {}
// 표시 문구와 다음 실행의 의존성을 함께 계산하되 실제 기능은 건드리지 않는 함수.
QVariantList FeatureSettingsController::modules() const {
    const auto plan = resolveFeatureStartup(draft_);
    QVariantList result;
    const auto append = [&](const char* id, const QString& title, const QString& description, bool requested, bool effective, bool active,
                            const QString& reason = {}) {
        result.append(QVariantMap{{"id", id}, {"title", title}, {"description", description},
            {"requested", requested}, {"effective", effective}, {"active", active}, {"reason", reason}});
    };
    append("file-organizer", tr("파일 정리"), tr("대기 목록, 분류 규칙, 이동·되돌리기와 작업 기록"), draft_.organizer, plan.active.organizer, active_.organizer);
    append("folder-watch", tr("폴더 감시"), tr("폴더를 지켜보고 조건에 맞으면 정리를 제안해요."), draft_.watch, plan.active.watch, active_.watch,
        draft_.watch && !draft_.organizer ? tr("파일 정리가 필요해요. 현재 선택으로 저장하면 폴더 감시도 시작하지 않아요.") : QString());
    append("notifications", tr("알림"), tr("기능에서 보내는 알림과 종류별 표시 설정"), draft_.notifications, plan.active.notifications, active_.notifications);
    append("companion", tr("컴패니언"), tr("바탕화면 컴패니언, 캐릭터 팩과 애니메이션 편집"), draft_.companion, plan.active.companion, active_.companion);
    append("companion-feeding", tr("먹이 주기"), tr("컴패니언 우클릭 메뉴에서 짧은 먹기 반응을 보여줘요."), draft_.feeding, plan.active.feeding, active_.feeding,
        draft_.feeding && !draft_.companion ? tr("컴패니언이 필요해요. 현재 선택으로 저장하면 먹이 주기도 시작하지 않아요.") : QString());
    return result;
}
// 화면의 모듈 선택을 바꾸고 잘못된 식별자는 저장되지 않게 거부하는 함수.
void FeatureSettingsController::setEnabled(const QString& id, bool enabled) {
    bool* selected = nullptr;
    if (id == "file-organizer") selected = &draft_.organizer;
    else if (id == "folder-watch") selected = &draft_.watch;
    else if (id == "notifications") selected = &draft_.notifications;
    else if (id == "companion") selected = &draft_.companion;
    else if (id == "companion-feeding") selected = &draft_.feeding;
    if (!selected) { status_ = tr("알 수 없는 모듈이에요."); emit changed(); return; }
    if (*selected == enabled) return;
    *selected = enabled;
    status_.clear();
    emit changed();
}
// 저장 성공 후에만 확정 선택을 교체하고 실패하면 초안을 그대로 남기는 함수.
bool FeatureSettingsController::save() {
    if (!canSave()) return true;
    const auto result = save_(draft_);
    if (!result) { status_ = QString::fromStdString(result.error().message); emit changed(); return false; }
    saved_ = draft_;
    invalidSaved_ = false;
    status_ = restartRequired() ? tr("저장했어요. 트레이의 ‘완전 종료’로 닫은 뒤 다시 실행하면 적용돼요.") : tr("모듈 선택을 저장했어요.");
    emit changed();
    return true;
}
// 저장된 선택으로 초안만 복원하고 이번 실행 기능은 바꾸지 않는 함수.
void FeatureSettingsController::discard() {
    draft_ = saved_;
    status_ = invalidSaved_ ? tr("모듈 설정을 읽지 못했어요. 선택을 확인한 뒤 저장해 주세요.") : QString();
    emit changed();
}
}

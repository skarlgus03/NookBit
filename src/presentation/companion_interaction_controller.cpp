#include "presentation/companion_interaction_controller.hpp"
#include <QThread>
#include <QVariantMap>
#include <utility>

namespace nookbit {
// 입력과 표현을 외부 창구로만 연결하며 별도 작업 스레드를 만들지 않는 생성자.
CompanionInteractionController::CompanionInteractionController(InteractionManager& manager, std::string targetId,
    ReadContext context, QObject* parent) : QObject(parent), manager_(manager), targetId_(std::move(targetId)), context_(std::move(context)) {
    reactionDeadline_.setSingleShot(true);
    connect(&reactionDeadline_, &QTimer::timeout, this, &CompanionInteractionController::clearReaction);
}
// 등록 모듈의 이름과 조건을 화면 값으로 바꾸며 처리기를 직접 실행하지 않는 함수.
QVariantList CompanionInteractionController::menuActions() const {
    QVariantList result;
    for (const auto& action : manager_.menuActions(targetId_, context_()))
        result.append(QVariantMap{{"id", QString::fromStdString(action.type.id)}, {"label", QString::fromStdString(action.type.name)}, {"enabled", action.enabled}});
    return result;
}
// 단발 요청은 세션을 만들지 않고 즉시 처리하는 함수.
bool CompanionInteractionController::invoke(const QString& typeId, double x, double y) {
    return start(typeId, InteractionPhase::Invoke, x, y);
}
// 하나의 연속 입력이 진행 중이면 다른 입력을 시작하지 않는 함수.
bool CompanionInteractionController::begin(const QString& typeId, double x, double y) {
    return start(typeId, InteractionPhase::Begin, x, y);
}
// 최초 상황을 보관해 연속 입력의 중간에 발생 문맥이 바뀌지 않게 하는 함수.
bool CompanionInteractionController::start(const QString& typeId, InteractionPhase phase, double x, double y) {
    if (QThread::currentThread() != thread() || session_) return false;
    if (nextId_ == 0) return false;
    return deliver({nextId_++, typeId.toStdString(), targetId_, phase, context_(), InteractionPoint{x, y}, {}});
}
// 최근 접촉 위치만 바꾸며 원래 요청 ID와 상황은 유지하는 함수.
bool CompanionInteractionController::update(double x, double y) {
    if (QThread::currentThread() != thread() || !session_) return false;
    auto request = *session_;
    request.phase = InteractionPhase::Update;
    request.point = InteractionPoint{x, y};
    return deliver(request);
}
// 마지막 유효한 접촉 위치로 연속 입력을 완료하는 함수.
bool CompanionInteractionController::end() {
    if (QThread::currentThread() != thread() || !session_) return false;
    auto request = *session_;
    request.phase = InteractionPhase::End;
    return deliver(request);
}
// 관리자 결과를 화면 값으로 바꾸고 단발 반응에만 타이머를 거는 함수.
bool CompanionInteractionController::deliver(const InteractionRequest& request) {
    const auto outcome = manager_.submit(request);
    lastReason_ = QString::fromStdString(outcome.reason);
    if (outcome.status == InteractionStatus::Rejected) {
        if (session_) {
            manager_.cancel(targetId_);
            session_.reset();
            clearReaction();
        }
        emit interactionChanged();
        return false;
    }
    if (outcome.status == InteractionStatus::Accepted) session_ = request;
    else session_.reset();
    if (outcome.reaction) {
        reactionDeadline_.stop();
        const auto& reaction = *outcome.reaction;
        emit reactionRequested(QString::number(request.instanceId), QString::fromStdString(reaction.cueId), QString::fromStdString(reaction.message));
        if (reaction.durationMs > 0) reactionDeadline_.start(reaction.durationMs);
    } else if (!session_) clearReaction();
    emit interactionChanged();
    return true;
}
// 일시 반응이 끝나면 기본 행동으로 돌아가도록 알리는 함수.
void CompanionInteractionController::clearReaction() {
    reactionDeadline_.stop();
    emit reactionRequested({}, {}, {});
}
// 대상의 입력 세션과 일시 반응을 취소하는 함수.
void CompanionInteractionController::cancel() {
    if (QThread::currentThread() != thread()) return;
    manager_.cancel(targetId_);
    session_.reset();
    clearReaction();
    emit interactionChanged();
}
// 숨김 중 입력을 차단하고 다시 표시하면 새 조작부터 받는 함수.
void CompanionInteractionController::setAvailable(bool available) {
    if (QThread::currentThread() != thread()) return;
    if (!available) cancel();
    manager_.setTargetAvailable(targetId_, available);
    emit interactionChanged();
}
}

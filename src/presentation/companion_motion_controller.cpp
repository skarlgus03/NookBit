#include "presentation/companion_motion_controller.hpp"
#include <QScopedValueRollback>
#include <QThread>
#include <cmath>
#include <algorithm>
#include <utility>

namespace nookbit {
// 별도 스레드나 휴식 중 프레임 반복 없이 동작하는 생성자.
CompanionMotionController::CompanionMotionController(ReadContext read, ApplyStep apply, bool enabled, IdleWalkConfig config, QObject* parent)
    : QObject(parent), read_(std::move(read)), apply_(std::move(apply)), enabled_(enabled) {
    if (!state_.configure(config)) enabled_ = false;
    timer_.setSingleShot(true);
    elapsed_.start();
    connect(&timer_, &QTimer::timeout, this, &CompanionMotionController::refresh);
}
// 비활성화 시 현재 걷기도 즉시 중단하는 함수.
void CompanionMotionController::setEnabled(bool enabled) {
    if (QThread::currentThread() != thread() || enabled_ == enabled) return;
    enabled_ = enabled;
    refresh();
    emit enabledChanged();
}
// 실제 클릭·드래그로 확정되기 전부터 사용자 조작을 우선하는 함수.
void CompanionMotionController::setPointerHeld(bool held) {
    if (QThread::currentThread() != thread() || pointerHeld_ == held) return;
    pointerHeld_ = held;
    refresh();
}
// 비정상 표시 범위는 무시하고 새 폭에 맞춰 걷기 경계를 갱신하는 함수.
void CompanionMotionController::setVisualBounds(double left, double right) {
    if (QThread::currentThread() != thread() || !std::isfinite(left) || !std::isfinite(right) || left >= right ||
        left < -2048 || right > 2048 || (visualLeft_ == left && visualRight_ == right)) return;
    visualLeft_ = left;
    visualRight_ = right;
    refresh();
}
// 이전 팩의 걷기 진행 위치를 새 팩에 이어 적용하지 않는 함수.
void CompanionMotionController::restart() {
    if (QThread::currentThread() != thread() || updating_) return;
    const bool wasWalking = state_.walking();
    state_.reset();
    wasAllowed_ = false;
    refresh();
    if (wasWalking) emit motionChanged();
}
// 최신 상황을 읽고 이동·행동 태그를 적용한 다음 필요한 시점만 예약하는 함수.
void CompanionMotionController::refresh() {
    if (QThread::currentThread() != thread() || updating_) return;
    QScopedValueRollback guard(updating_, true);
    auto context = read_();
    context.allowed = context.allowed && enabled_ && !pointerHeld_;
    context.left -= std::min(visualLeft_, 0.0);
    context.right -= std::max(visualRight_, 0.0);
    const auto elapsedMs = elapsed_.restart();
    const bool wasWalking = state_.walking();
    const auto step = state_.advance(wasAllowed_ ? static_cast<double>(elapsedMs) : 0.0, context);
    wasAllowed_ = context.allowed;
    apply_(step);
    if (wasWalking != step.walking) emit motionChanged();
    if (context.allowed) timer_.start(state_.nextDelayMs());
    else timer_.stop();
}
}

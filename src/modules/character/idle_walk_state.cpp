#include "modules/character/idle_walk_state.hpp"
#include <algorithm>
#include <cmath>

namespace nookbit {
// 0 이하의 시간이나 비정상 속도로 바쁜 반복이 생기지 않게 검증하는 함수.
Result<void> IdleWalkState::configure(IdleWalkConfig config) {
    if (config.idleMs < 100 || config.idleMs > 600000 || config.walkMs < 100 || config.walkMs > 60000 ||
        !std::isfinite(config.pixelsPerSecond) || config.pixelsPerSecond <= 0 || config.pixelsPerSecond > 500)
        return Result<void>::failure({"motion.invalid_config", "Invalid idle/walk timing or speed."});
    config_ = config;
    reset();
    return Result<void>::success();
}
// 중단 전 경로나 시간으로 복귀하지 않고 새 휴식을 시작하는 함수.
void IdleWalkState::reset() {
    if (walking_) nextDirection_ = -direction_;
    walking_ = false;
    idleElapsed_ = 0;
    walkElapsed_ = 0;
}
// 모션은 시간으로 계산하며 프레임 수나 이미지 재생 시간에 의존하지 않는 함수.
IdleWalkStep IdleWalkState::advance(double elapsedMs, const IdleWalkContext& context) {
    if (!context.allowed || !std::isfinite(context.x) || !std::isfinite(context.left) || !std::isfinite(context.right) ||
        context.left >= context.right || context.x < context.left || context.x > context.right ||
        !std::isfinite(elapsedMs) || elapsedMs < 0) {
        reset();
        return {false, false, context.x, direction_};
    }
    if (!walking_) {
        idleElapsed_ += elapsedMs;
        if (idleElapsed_ < config_.idleMs) return {false, false, context.x, direction_};
        direction_ = nextDirection_;
        const auto room = [&context](int direction) { return direction < 0 ? context.x - context.left : context.right - context.x; };
        if (room(direction_) < 16) direction_ = -direction_;
        if (room(direction_) < 16) { reset(); return {false, false, context.x, direction_}; }
        x_ = context.x;
        idleElapsed_ = 0;
        walkElapsed_ = 0;
        walking_ = true;
        return {true, false, x_, direction_};
    }
    // 사용자가 위치를 바꾼 경우 예전 경로로 끌어당기지 않는다.
    if (std::abs(context.x - std::round(x_)) > 1) {
        reset();
        return {false, false, context.x, direction_};
    }
    const auto oldX = x_;
    const auto stepMs = std::min({elapsedMs, 100.0, std::max(0.0, config_.walkMs - walkElapsed_)});
    x_ = std::clamp(x_ + direction_ * config_.pixelsPerSecond * stepMs / 1000.0, context.left, context.right);
    walkElapsed_ += elapsedMs;
    const bool finished = walkElapsed_ >= config_.walkMs || x_ <= context.left || x_ >= context.right;
    if (finished) reset();
    return {walking_, x_ != oldX, x_, direction_};
}
// 걷기 중에만 짧은 갱신을 요청하고 휴식은 남은 시간을 한 번에 기다리는 함수.
int IdleWalkState::nextDelayMs() const {
    return walking_ ? 16 : std::max(1, static_cast<int>(std::ceil(config_.idleMs - idleElapsed_)));
}
}

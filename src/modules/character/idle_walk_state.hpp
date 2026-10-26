#pragma once
#include <nookbit/domain/result.hpp>

namespace nookbit {
struct IdleWalkConfig {
    int idleMs{6000};
    int walkMs{3000};
    double pixelsPerSecond{36};
};
struct IdleWalkContext {
    bool allowed{false};
    double x{0};
    double left{0};
    double right{0};
};
struct IdleWalkStep {
    bool walking{false};
    bool move{false};
    double x{0};
    int direction{-1};
};

/// 실제 시간과 수평 범위로 휴식·걷기를 선택하며 그림·Qt·파일 작업을 모르는 상태기.
class IdleWalkState final {
public:
    /// 시간과 이동 속도를 검사하여 동작 설정을 교체하는 함수.
    [[nodiscard]] Result<void> configure(IdleWalkConfig config);
    /// 경과 시간과 현재 환경을 받아 다음 표현·이동 명령을 계산하는 함수.
    [[nodiscard]] IdleWalkStep advance(double elapsedMs, const IdleWalkContext& context);
    /// 진행 중 걷기를 버리고 새 휴식 구간부터 시작하는 함수.
    void reset();
    /// 현재 걷기 구간인지 반환하는 함수.
    bool walking() const { return walking_; }
    /// 다음 계산까지 필요한 시간을 반환하는 함수. 휴식 중에는 프레임 타이머가 필요 없다.
    int nextDelayMs() const;
private:
    IdleWalkConfig config_;
    bool walking_{false};
    double idleElapsed_{0}, walkElapsed_{0}, x_{0};
    int direction_{-1}, nextDirection_{-1};
};
}

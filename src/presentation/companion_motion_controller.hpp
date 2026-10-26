#pragma once
#include "modules/character/idle_walk_state.hpp"
#include <QObject>
#include <QElapsedTimer>
#include <QTimer>
#include <functional>

namespace nookbit {
/// 현재 환경 조회와 모션 적용을 주입받아 자율 걷기의 시간만 조율하는 어댑터.
class CompanionMotionController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool walking READ walking NOTIFY motionChanged)
public:
    using ReadContext = std::function<IdleWalkContext()>;
    using ApplyStep = std::function<void(const IdleWalkStep&)>;
    /// 상황과 출력 창구를 전달받고 아직 타이머를 시작하지 않는 생성자.
    CompanionMotionController(ReadContext read, ApplyStep apply, bool enabled, IdleWalkConfig config = {}, QObject* parent = nullptr);
    /// 자율 걷기 사용 여부를 반환하는 함수.
    bool enabled() const { return enabled_; }
    /// 현재 걷는 중인지 반환하는 함수.
    bool walking() const { return state_.walking(); }
    /// 사용 설정을 바꾸고 현재 동작을 다시 판단하는 함수.
    Q_INVOKABLE void setEnabled(bool enabled);
    /// 포인터를 누르는 동안 자율 이동을 중지하는 함수.
    Q_INVOKABLE void setPointerHeld(bool held);
    /// 렌더러가 계산한 피벗 기준 표시 폭을 받아 화면 밖 이동을 막는 함수.
    Q_INVOKABLE void setVisualBounds(double left, double right);
    /// 알림·작업·표시 상태가 바뀌었을 때 대기 또는 중단을 갱신하는 함수.
    void refresh();
    /// 팩 교체 시 이전 이동 경로를 버리고 새 휴식부터 시작하는 함수.
    void restart();
signals:
    void enabledChanged();
    void motionChanged();
private:
    ReadContext read_;
    ApplyStep apply_;
    IdleWalkState state_;
    QTimer timer_;
    QElapsedTimer elapsed_;
    bool enabled_{true}, pointerHeld_{false}, updating_{false}, wasAllowed_{false};
    double visualLeft_{-40}, visualRight_{40};
};
}

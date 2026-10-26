#pragma once
#include "modules/character/interaction_manager.hpp"
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

namespace nookbit {
/// 화면의 의미 있는 입력을 공통 창구에 전달하고 일시 반응의 표시 시간만 관리한다.
class CompanionInteractionController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY interactionChanged)
    Q_PROPERTY(QString lastReason READ lastReason NOTIFY interactionChanged)
public:
    using ReadContext = std::function<InteractionContext()>;
    /// 효과 관리자와 현재 상황 조회 창구를 주입하는 생성자.
    CompanionInteractionController(InteractionManager& manager, std::string targetId, ReadContext context, QObject* parent = nullptr);
    /// 연속 입력이 진행 중인지 반환하는 함수.
    bool active() const { return session_.has_value(); }
    /// 최근 거절 이유 코드를 반환하는 함수.
    QString lastReason() const { return lastReason_; }
    /// 단발 상호작용을 요청하는 함수.
    Q_INVOKABLE bool invoke(const QString& typeId, double x, double y);
    /// 연속 입력을 시작하는 함수.
    Q_INVOKABLE bool begin(const QString& typeId, double x, double y);
    /// 현재 접촉 위치를 갱신하는 함수.
    Q_INVOKABLE bool update(double x, double y);
    /// 진행 중인 연속 입력을 완료하는 함수.
    Q_INVOKABLE bool end();
    /// 입력 상실·팩 교체 시 세션과 일시 반응을 취소하는 함수.
    Q_INVOKABLE void cancel();
    /// 대상 숨김·표시에 맞춰 입력 가능 여부를 갱신하는 함수.
    void setAvailable(bool available);
signals:
    void interactionChanged();
    void reactionRequested(const QString& instanceId, const QString& cueId, const QString& message);
private:
    /// 새 요청의 상황과 식별자를 채워 관리자에게 전달하는 함수.
    bool start(const QString& typeId, InteractionPhase phase, double x, double y);
    /// 결과에 맞춰 반응을 표시하고 세션을 닫는 함수.
    bool deliver(const InteractionRequest& request);
    /// 현재 반응의 타이머와 표시를 해제하는 함수.
    void clearReaction();
    InteractionManager& manager_;
    std::string targetId_;
    ReadContext context_;
    std::optional<InteractionRequest> session_;
    std::uint64_t nextId_{1};
    QString lastReason_;
    QTimer reactionDeadline_;
};
}

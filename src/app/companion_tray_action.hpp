#pragma once
#include <QObject>
#include <QPointer>
#include <QAction>

class QMenu;
namespace nookbit {
class CharacterWindowController;

/// 컴패니언 표시 메뉴와 상태 동기화만 소유하는 트레이 기여 객체.
/// 메뉴·컴패니언 어느 쪽이 먼저 종료돼도 남은 창구를 호출하지 않는다.
class CompanionTrayAction final : public QObject {
public:
    /// 외부 메뉴에 소유한 동작을 넣고 표시 상태를 양방향으로 연결하는 생성자.
    CompanionTrayAction(QMenu& menu, CharacterWindowController& placement);
    /// 메뉴가 살아 있다면 동작을 제거하고 신호 연결을 정리하는 소멸자.
    ~CompanionTrayAction() override;
private:
    QPointer<QMenu> menu_;
    QPointer<CharacterWindowController> placement_;
    QAction action_;
};
}

#include "app/companion_tray_action.hpp"
#include "presentation/character_window_controller.hpp"
#include <QMenu>

namespace nookbit {
// 표시 명령과 외부 표시 변경만 연결하고 앱 종료나 파일 작업은 알지 않는 생성자.
CompanionTrayAction::CompanionTrayAction(QMenu& menu, CharacterWindowController& placement)
    : menu_(&menu), placement_(&placement), action_(QStringLiteral("캐릭터 표시")) {
    action_.setObjectName(QStringLiteral("companionVisibilityAction"));
    action_.setCheckable(true);
    action_.setChecked(placement.shown());
    menu.addAction(&action_);
    QObject::connect(&action_, &QAction::triggered, this, [this](bool shown) {
        // 연결 대상이 남아 있을 때만 컴패니언의 노출을 변경하는 함수.
        if (placement_) placement_->setShown(shown);
    });
    QObject::connect(&placement, &CharacterWindowController::visibilityChanged, this, [this] {
        // 설정 화면이나 다른 입력의 변경을 트레이 체크 표시와 일치시키는 함수.
        if (placement_) action_.setChecked(placement_->shown());
    });
    QObject::connect(&placement, &QObject::destroyed, this, [this] {
        // 기능이 종료되면 호스트 메뉴에서 사용할 수 없는 동작을 제거하는 함수.
        if (menu_) menu_->removeAction(&action_);
        action_.setEnabled(false);
    });
}

// QAction은 이 객체가 소유하므로 외부 메뉴에 수명을 맡기지 않는 소멸자.
CompanionTrayAction::~CompanionTrayAction() {
    if (menu_) menu_->removeAction(&action_);
}
}

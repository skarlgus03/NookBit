#include "app/companion_tray_action.hpp"
#include "presentation/character_window_controller.hpp"
#include <QApplication>
#include <QMenu>
#include <QPointer>
#include <iostream>
#include <memory>

namespace {
int failures{};
// 표시 명령과 수명 처리의 예상 결과가 다르면 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 메뉴와 설정의 표시 상태가 일치하고 기여 객체만 종료해도 메뉴가 정리되는지 검사하는 함수.
void testVisibilityAndBindingLifetime() {
    nookbit::CharacterWindowController placement({QRect(0, 0, 1000, 800)}, QPoint(500, 500), true);
    QMenu menu;
    auto* hostAction = menu.addAction("Host action");
    QPointer<QAction> contributed;
    {
        nookbit::CompanionTrayAction binding(menu, placement);
        contributed = menu.actions().last();
        expect(menu.actions().size() == 2 && contributed->objectName() == "companionVisibilityAction"
            && contributed->isCheckable() && contributed->isChecked(), "contribution reflects initial visibility");
        contributed->trigger();
        expect(!placement.shown() && !contributed->isChecked(), "tray action hides companion");
        placement.setShown(true);
        expect(contributed->isChecked(), "external settings change updates tray check");
    }
    expect(contributed.isNull() && menu.actions().size() == 1 && menu.actions().first() == hostAction,
        "destroying binding removes its action and preserves host actions");
    placement.setShown(false);
}
// 컴패니언이 먼저 종료되면 메뉴에 실행 불가능한 항목이 남지 않는지 검사하는 함수.
void testFeatureFirst() {
    auto placement = std::make_unique<nookbit::CharacterWindowController>(
        QList<QRect>{QRect(0, 0, 1000, 800)}, QPoint(500, 500), true);
    QMenu menu;
    auto binding = std::make_unique<nookbit::CompanionTrayAction>(menu, *placement);
    QPointer<QAction> action = menu.actions().first();
    placement.reset();
    expect(menu.actions().isEmpty() && action && !action->isEnabled(), "feature destruction removes and disables contributed action");
    action->trigger();
    binding.reset();
    expect(action.isNull(), "binding later destroys its detached action");
}
// 메뉴가 먼저 종료돼도 기여 객체와 컴패니언을 정상적으로 해제할 수 있는지 검사하는 함수.
void testMenuFirst() {
    nookbit::CharacterWindowController placement({QRect(0, 0, 1000, 800)}, QPoint(500, 500), false);
    auto menu = std::make_unique<QMenu>();
    auto binding = std::make_unique<nookbit::CompanionTrayAction>(*menu, placement);
    QPointer<QAction> action = menu->actions().first();
    menu.reset();
    placement.setShown(true);
    expect(action && action->isChecked(), "binding safely survives external menu destruction");
    binding.reset();
    expect(action.isNull() && placement.shown(), "binding destruction leaves companion state intact");
}
}
// 시스템 트레이나 실제 창을 띄우지 않고 메뉴 기여의 명령·수명을 검사하는 함수.
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    testVisibilityAndBindingLifetime();
    testFeatureFirst();
    testMenuFirst();
    if (!failures) std::cout << "All companion tray-contribution tests passed.\n";
    return failures ? 1 : 0;
}

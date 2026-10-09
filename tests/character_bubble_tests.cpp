#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QPointF>
#include <QRectF>
#include <iostream>
#include <memory>

namespace {
int failures = 0;
// 배치 조건이 다르면 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 음수 화면과 네 모서리에서도 말풍선이 작업 영역 안에 있는지 검사하는 함수.
void checkArea(QQuickWindow& bubble, QRectF screen, QPointF anchor, bool left, bool below) {
    bubble.setProperty("screenArea", screen);
    bubble.setProperty("anchorPoint", anchor);
    QCoreApplication::processEvents();
    expect(bubble.x() >= screen.x() + 7 && bubble.y() >= screen.y() + 7, "bubble inside left/top margin");
    expect(bubble.x() + bubble.width() <= screen.right() - 7 && bubble.y() + bubble.height() <= screen.bottom() - 7,
        "bubble inside right/bottom margin");
    expect(bubble.property("flippedLeft").toBool() == left, "horizontal direction chosen");
    expect(bubble.property("belowAnchor").toBool() == below, "vertical direction chosen");
}
}
// 실제 말풍선 QML의 방향 전환·줄바꿈·확인 버튼·투명 창을 검사하는 함수.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/presentation/qml/CharacterBubble.qml"));
    std::unique_ptr<QObject> object(component.createWithInitialProperties({
        {"screenArea", QRectF(0, 0, 800, 600)}, {"anchorPoint", QPointF(200, 300)},
        {"message", QStringLiteral("말풍선 테스트입니다.")}}));
    if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
    auto* bubble = qobject_cast<QQuickWindow*>(object.get());
    if (!bubble) return 1;
    expect(bubble->color().alpha() == 0 && bubble->flags().testFlag(Qt::FramelessWindowHint), "transparent frameless bubble");
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(200, 300), false, false);
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(790, 590), true, false);
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(790, 10), true, true);
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(10, 10), false, true);
    checkArea(*bubble, QRectF(-1280, -200, 1280, 960), QPointF(-1270, -190), false, true);
    bubble->setProperty("actionAvailable", true);
    bubble->setProperty("message", QStringLiteral("문장이 길어도 말풍선은 줄을 바꾸며 표시합니다. 화면 가장자리에서도 보이지 않는 곳으로 나가지 않아야 합니다."));
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(790, 590), true, false);
    auto* action = bubble->findChild<QObject*>(QStringLiteral("characterBubbleAction"));
    expect(action && action->property("visible").toBool(), "action displayed when available");
    expect(QMetaObject::invokeMethod(bubble, "actionRequested"), "generic action signal available");
    bubble->setProperty("actionAvailable", false);
    expect(action && !action->property("visible").toBool(), "action hidden without proposal");
    return failures ? 1 : 0;
}

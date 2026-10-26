#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QPointF>
#include <QRectF>
#include <iostream>
#include <memory>
#include <QVariantMap>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>

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
    // 독립 QML 검사에도 앱에서 사용하는 실제 공통 규격을 제공하는 함수 흐름.
    QQmlComponent metricsComponent(&engine, QUrl::fromLocalFile(QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/src/presentation/qml/UiMetrics.qml"));
    std::unique_ptr<QObject> metrics(metricsComponent.create());
    if (!metrics) { std::cerr << metricsComponent.errorString().toStdString(); return 1; }
    engine.rootContext()->setContextProperty(QStringLiteral("UiMetrics"), metrics.get());
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
    bubble->setProperty("configuration", QVariantMap{{"pivot", QVariantMap{{"x", 0.25}, {"y", 1}}}});
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(200, 300), false, false);
    auto pivot = bubble->property("effectivePivot").toPointF();
    expect(std::abs(bubble->x() + pivot.x() - 200) <= 1 && std::abs(bubble->y() + pivot.y() - 300) <= 1,
        "bubble local pivot aligns with character attachment point");
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(790, 10), true, true);
    pivot = bubble->property("effectivePivot").toPointF();
    expect(std::abs(pivot.x() - bubble->width() * 0.75) <= 1 && pivot.y() == 0,
        "both pivot axes transformed during flipping");
    const auto source = QUrl::fromLocalFile(QString::fromUtf8(NOOKBIT_SOURCE_DIR) + "/resources/character/sprites/test-character.png");
    const QVariantMap skin{{"kind", "image"}, {"source", source}, {"imageWidth", 512}, {"imageHeight", 384},
        {"width", 270}, {"height", 120},
        {"textRect", QVariantMap{{"x", 14}, {"y", 12}, {"width", 242}, {"height", 42}}},
        {"actionRect", QVariantMap{{"x", 194}, {"y", 72}, {"width", 64}, {"height", 28}}}};
    bubble->setProperty("configuration", QVariantMap{{"skin", skin}});
    bubble->show();
    QElapsedTimer deadline; deadline.start();
    while (!bubble->property("customReady").toBool() && deadline.elapsed() < 2000) {
        QCoreApplication::processEvents(); QThread::msleep(1);
    }
    expect(bubble->property("customReady").toBool(), "custom PNG background decodes");
    checkArea(*bubble, QRectF(0, 0, 800, 600), QPointF(790, 10), true, true);
    const auto area = bubble->property("actionArea").toMap();
    expect(area.value("x").toDouble() == 12 && area.value("y").toDouble() == 20, "custom action area mirrored without flipping text");
    bubble->setProperty("configuration", QVariantMap{});
    expect(!bubble->property("customSkin").toBool(), "switching to default resets custom visual");
    bubble->setProperty("message", QString(3000, QChar(0xAC00)));
    QCoreApplication::processEvents();
    expect(bubble->width() <= 320 && bubble->height() <= 168, "long messages keep bounded default bubble size");
    auto* body = bubble->findChild<QObject*>(QStringLiteral("characterBubbleText"));
    expect(body && body->property("maximumLineCount").toInt() == 4, "shared body limits long text to four lines");
    auto* view = bubble->findChild<QObject*>(QStringLiteral("characterBubbleView"));
    expect(view != nullptr, "shared bubble view must be available for preview placement");
    if (view) {
        view->setProperty("constrainToArea", false);
        bubble->setProperty("screenArea", QRectF(0, 0, 80, 60));
        bubble->setProperty("anchorPoint", QPointF(20, -300));
        expect(!view->property("belowAnchor").toBool() && !view->property("flippedLeft").toBool() &&
            view->property("placementY").toDouble() < -300 && view->property("width").toDouble() == 320,
            "editor preview must preserve offscreen attachment and native bubble dimensions without flipping");
    }
    return failures ? 1 : 0;
}

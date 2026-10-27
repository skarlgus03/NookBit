#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QJSValue>
#include <QColor>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QEventLoop>
#include <QTimer>
#include <memory>
#include <iostream>
#include <cmath>

namespace {
int failures = 0;

// 편집 상태와 화면 연결의 조건이 다르면 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// 앱 전역 객체 없이 명시적인 입력으로 편집기 부품을 생성하는 함수.
std::unique_ptr<QObject> create(QQmlEngine& engine, const char* name, const QVariantMap& properties = {}) {
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(NOOKBIT_SOURCE_DIR)
        + "/src/presentation/qml/" + QString::fromLatin1(name) + ".qml"));
    std::unique_ptr<QObject> result(component.createWithInitialProperties(properties));
    if (!result) { ++failures; std::cerr << component.errorString().toStdString(); }
    return result;
}

// 편집 명령을 실제 QML 함수로 실행하고 스크립트 오류도 실패로 기록하는 함수.
QJSValue call(QQmlEngine& engine, QObject* target, const char* name, const QVariantList& values = {}) {
    const auto object = engine.newQObject(target);
    QJSValueList args;
    for (const auto& value : values) args.append(engine.toScriptValue(value));
    const auto result = object.property(QString::fromLatin1(name)).callWithInstance(object, args);
    if (result.isError()) { ++failures; std::cerr << name << ": " << result.toString().toStdString() << '\n'; }
    return result;
}

// 편집 세션이 소유한 JS 배열을 비교 가능한 값 목록으로 읽는 함수.
QVariantList list(QObject* target, const char* name) {
    return target->property(name).value<QJSValue>().toVariant().toList();
}

// 실제 화면 트리와 팝업의 이벤트 반영을 기다리는 함수.
void settle() {
    QEventLoop events;
    QTimer::singleShot(100, &events, &QEventLoop::quit);
    events.exec();
}

// sRGB 글씨와 배경의 상대 휘도 대비를 계산하는 함수.
double contrast(const QColor& first, const QColor& second) {
    // 색상 채널을 선형 광량으로 바꾸는 함수.
    const auto channel = [](double value) { return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4); };
    // 표준 가중치로 색상의 상대 휘도를 구하는 함수.
    const auto luminance = [&](const QColor& color) {
        return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
    };
    const double a = luminance(first), b = luminance(second);
    return (qMax(a, b) + 0.05) / (qMin(a, b) + 0.05);
}

// 그리기에 전달하는 제어점으로 베지어 곡선의 한 점을 계산하는 함수.
QPointF curvePoint(const QVariantMap& curve, double t) {
    const double inverse = 1 - t;
    return {
        inverse * inverse * inverse * curve.value("x1").toDouble()
            + 3 * inverse * inverse * t * curve.value("cx1").toDouble()
            + 3 * inverse * t * t * curve.value("cx2").toDouble() + t * t * t * curve.value("x2").toDouble(),
        inverse * inverse * inverse * curve.value("y1").toDouble()
            + 3 * inverse * inverse * t * curve.value("cy1").toDouble()
            + 3 * inverse * t * t * curve.value("cy2").toDouble() + t * t * t * curve.value("y2").toDouble()
    };
}
}

// 분리된 편집 상태·노드 입력·저장 연결을 파일 작업 없이 검증하는 함수.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    auto metrics = create(engine, "UiMetrics");
    auto theme = create(engine, "UiTheme");
    if (!metrics || !theme) return 1;
    engine.rootContext()->setContextProperty("UiMetrics", metrics.get());
    engine.rootContext()->setContextProperty("UiTheme", theme.get());
    bool cleanBindings = true;
    // 숨은 페이지 참조나 잘못된 바인딩이 생기면 테스트를 실패시키는 함수.
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
        cleanBindings = false;
        for (const auto& error : errors) std::cerr << error.toString().toStdString() << '\n';
    });
    auto state = create(engine, "RuleEditorState", {{"viewportWidth", 1000}, {"viewportHeight", 720}});
    auto other = create(engine, "RuleEditorState");
    if (!state || !other) return 1;
    for (const char* mode : {"light", "dark"}) {
        const auto colors = theme->property(mode).value<QJSValue>().toVariant().toMap();
        const QColor foreground(colors.value("accentText").toString());
        for (const char* role : {"accent", "primaryHover", "primaryPressed"})
            expect(contrast(foreground, QColor(colors.value(role).toString())) >= 4.5,
                "primary button text remains readable in normal/hover/press for both themes");
        expect(contrast(QColor(colors.value("accentSoftText").toString()), QColor(colors.value("menuSelected").toString())) >= 4.5,
            "selected navigation text contrasts with its soft background");
        for (const QString role : {QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("danger")})
            expect(contrast(QColor(colors.value(role).toString()), QColor(colors.value(role + "Surface").toString())) >= 4.5,
                "semantic status text remains readable on its corresponding background");
        const QColor accent(colors.value("accent").toString()), marquee = colors.value("marquee").value<QColor>();
        expect(marquee.isValid() && qAbs(marquee.alphaF() - 0.2) < 0.01
            && qAbs(marquee.redF() - accent.redF()) < 0.01 && qAbs(marquee.blueF() - accent.blueF()) < 0.01,
            "selection tint derives from the actual typed theme accent");
    }
    for (const double zoom : {0.1, 0.25, 1.0, 2.5}) {
        state->setProperty("zoom", zoom);
        const auto curves = call(engine, state.get(), "connectionSegments", {0, 0, 20 * zoom, 80 * zoom}).toVariant().toList();
        expect(curves.size() == 1, "forward wires use one continuous curve");
        if (curves.isEmpty()) return 1;
        const auto curve = curves.first().toMap();
        expect(curve.value("cx1").toDouble() >= 0 && curve.value("cx1").toDouble() <= curve.value("cx2").toDouble()
            && curve.value("cx2").toDouble() <= 20 * zoom,
            "short wires cannot overshoot or fold when zoomed out");
    }
    state->setProperty("zoom", 1.0);
    const QVariantList reverseBlocks{
        QVariantMap{{"id", 1}, {"type", "start"}, {"x", 500}, {"y", 100}},
        QVariantMap{{"id", 2}, {"type", "extension"}, {"x", 100}, {"y", 100}}
    };
    const QVariantMap reversed{{"valid", true}, {"blocks", reverseBlocks},
        {"edges", QVariantList{QVariantMap{{"from", 1}, {"to", 2}}}}, {"nextId", 3}, {"incompleteLeaves", 1}};
    call(engine, state.get(), "restoreGraph", {reversed});
    const double sourceX = 500 + state->property("blockWidth").toDouble();
    const double centerY = 100 + state->property("blockHeight").toDouble() / 2;
    const auto reverseCurves = call(engine, state.get(), "connectionSegments", {sourceX, centerY, 100, centerY}).toVariant().toList();
    expect(reverseCurves.size() == 2, "same-height reverse wires detour instead of folding over a line");
    if (reverseCurves.size() != 2) return 1;
    const auto firstCurve = reverseCurves.first().toMap(), lastCurve = reverseCurves.last().toMap();
    expect(firstCurve.value("x2") == lastCurve.value("x1") && firstCurve.value("y2") == lastCurve.value("y1")
        && firstCurve.value("y2").toDouble() < centerY, "reverse curve segments share a smooth upper join");
    const auto sample = curvePoint(firstCurve, 0.5);
    expect(call(engine, state.get(), "edgeAt", {sample.x(), sample.y()}).toInt() == 0,
        "Alt selection follows the exact multi-segment curve used to draw the wire");
    call(engine, state.get(), "resetExample");
    const auto initialEdges = list(state.get(), "edges");
    expect(!call(engine, state.get(), "connectBlocks", {3, 2}).toBool()
        && list(state.get(), "edges") == initialEdges, "cycle rejection cannot mutate the graph");
    expect(!call(engine, state.get(), "connectBlocks", {4, 1}).toBool()
        && list(state.get(), "edges") == initialEdges, "terminal and input constraints remain enforced");
    call(engine, state.get(), "selectOnly", {2});
    call(engine, state.get(), "toggleSelection", {3});
    call(engine, state.get(), "copySelectedBlocks");
    call(engine, state.get(), "pasteCopiedBlocks");
    const auto copied = list(state.get(), "selectedIds");
    expect(list(state.get(), "blocks").size() == 6 && copied.size() == 2
        && list(state.get(), "edges").last().toMap().value("from") == copied.first()
        && list(state.get(), "edges").last().toMap().value("to") == copied.last(),
        "copy/paste assigns new ids and preserves only the internal copied connection");
    expect(list(other.get(), "blocks").size() == 4 && list(other.get(), "copiedBlocks").isEmpty(),
        "editing sessions cannot share a clipboard or graph by accident");

    call(engine, state.get(), "resetExample");
    call(engine, state.get(), "selectOnly", {2});
    call(engine, state.get(), "toggleSelection", {3});
    const QVariantMap starts{{"2", QVariantMap{{"x", 390}, {"y", 220}}},
        {"3", QVariantMap{{"x", 720}, {"y", 220}}}};
    call(engine, state.get(), "updateDraggedPositions", {2, starts, 0, 46, true});
    call(engine, state.get(), "commitDraggedBlocks");
    const auto snapped = list(state.get(), "blocks");
    expect(snapped[1].toMap().value("y").toInt() == 270 && snapped[2].toMap().value("y").toInt() == 270
        && list(state.get(), "edges") == initialEdges, "group snapping preserves spacing and rule order");
    state->setProperty("connectingFrom", 2);
    state->setProperty("draggingConnection", true);
    call(engine, state.get(), "finishConnectionAt", {-999, -999});
    expect(state->property("connectingFrom").toInt() == -1 && !state->property("draggingConnection").toBool()
        && list(state.get(), "edges") == initialEdges, "dropping a wire on empty space cancels without editing connections");
    call(engine, state.get(), "resetExample");
    const auto beforeZoom = call(engine, state.get(), "worldPoint", {400, 250});
    const double worldX = beforeZoom.property("x").toNumber(), worldY = beforeZoom.property("y").toNumber();
    call(engine, state.get(), "zoomAt", {-120, 400, 250});
    const auto afterZoom = call(engine, state.get(), "worldPoint", {400, 250});
    expect(qAbs(afterZoom.property("x").toNumber() - worldX) < 0.001
        && qAbs(afterZoom.property("y").toNumber() - worldY) < 0.001,
        "zoom preserves the world coordinate under the pointer");
    call(engine, state.get(), "resetExample");

    const QVariantMap tested{{"ok", true}, {"outcome", "destination"},
        {"path", QStringList{"graph-2", "graph-3"}}};
    call(engine, state.get(), "applyTestResult", {tested});
    expect(list(state.get(), "testTraceBlockIds") == QVariantList{1, 2, 3},
        "test result highlights the matching graph nodes in the one session");
    call(engine, state.get(), "updateValue", {2, "6"});
    expect(list(state.get(), "testTraceBlockIds").isEmpty()
        && state->property("testResult").value<QJSValue>().property("ok").isUndefined(),
        "editing a rule invalidates its previous test result and highlight");

    QQuickWindow host;
    host.resize(1000, 720);
    auto canvas = create(engine, "RuleCanvas", {{"editor", QVariant::fromValue(state.get())}, {"width", 1000}, {"height", 720}});
    if (!canvas) return 1;
    qobject_cast<QQuickItem*>(canvas.get())->setParentItem(host.contentItem());
    host.show();
    settle();
    // 분리한 노드와 캔버스에 실제 포인터 경로를 전달하는 함수.
    const auto pointer = [&](QEvent::Type type, QPointF point, Qt::MouseButton button,
        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, point, host.mapToGlobal(point.toPoint()), button, buttons, modifiers);
        QCoreApplication::sendEvent(&host, &event);
        QCoreApplication::processEvents();
    };
    pointer(QEvent::MouseButtonPress, {450, 260}, Qt::LeftButton, Qt::LeftButton);
    pointer(QEvent::MouseButtonRelease, {450, 260}, Qt::LeftButton, Qt::NoButton);
    expect(state->property("selectedId").toInt() == 2, "node click updates the one shared editing session");
    pointer(QEvent::MouseButtonPress, {800, 260}, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    pointer(QEvent::MouseButtonRelease, {800, 260}, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    expect(list(state.get(), "selectedIds").size() == 2, "shift selection still works after node extraction");
    pointer(QEvent::MouseButtonPress, {50, 40}, Qt::RightButton, Qt::RightButton);
    pointer(QEvent::MouseMove, {90, 65}, Qt::NoButton, Qt::RightButton);
    pointer(QEvent::MouseButtonRelease, {90, 65}, Qt::RightButton, Qt::NoButton);
    auto* addMenu = canvas->findChild<QObject*>("addNodeMenu");
    expect(addMenu && !addMenu->property("visible").toBool() && state->property("panX").toInt() == 40
        && state->property("panY").toInt() == 25, "right drag pans without opening the add menu");

    QQmlComponent stub(&engine);
    stub.setData(R"(import QtQuick
        QtObject {
            property bool dirty: false
            property bool allowImport: true
            property bool allowSave: true
            property string message: "Blocked"
            property int saves: 0
            property int layouts: 0
            signal rulesSaved()
            // 파일을 저장하지 않고 허용 여부만 반환하는 대역 함수.
            function importGraph(blocks, edges) { return allowImport }
            // 저장 완료 알림의 실제 연결 순서만 재현하는 대역 함수.
            function save() { ++saves; if (!allowSave) return false; rulesSaved(); return true }
            // 노드 위치 저장 요청을 세는 대역 함수.
            function saveGraphLayout(blocks, edges) { ++layouts; return true }
            // 파일을 읽지 않고 고정된 저장본을 반환하는 대역 함수.
            function graph() { return {valid: true, blocks: [{id: 1, type: "start", x: 70, y: 220, value: "Input"}], edges: [], nextId: 2, incompleteLeaves: 0} }
        })", QUrl());
    std::unique_ptr<QObject> controller(stub.create());
    if (!controller) { std::cerr << stub.errorString().toStdString(); return 1; }
    auto documents = create(engine, "RuleDocumentActions", {{"editor", QVariant::fromValue(state.get())},
        {"controller", QVariant::fromValue(controller.get())}, {"guideService", QVariant::fromValue(controller.get())},
        {"width", 1000}, {"height", 720}});
    if (!documents) return 1;
    qobject_cast<QQuickItem*>(documents.get())->setParentItem(host.contentItem());
    call(engine, documents.get(), "saveAll");
    settle();
    expect(controller->property("saves").toInt() == 1 && controller->property("layouts").toInt() == 1
        && !state->property("graphDirty").toBool() && !state->property("stagedGraph").toBool(),
        "save completion persists layout and reloads the same session exactly once");
    call(engine, state.get(), "addBlock", {"size", 300, 200});
    const auto unsaved = list(state.get(), "blocks");
    controller->setProperty("allowImport", false);
    call(engine, documents.get(), "saveAll");
    expect(controller->property("saves").toInt() == 1 && list(state.get(), "blocks") == unsaved
        && state->property("graphDirty").toBool() && state->property("statusText").toString() == "Blocked",
        "validation failure keeps the edited graph and cannot call save");
    controller->setProperty("allowImport", true);
    controller->setProperty("allowSave", false);
    call(engine, documents.get(), "saveAll");
    expect(controller->property("layouts").toInt() == 1 && list(state.get(), "blocks") == unsaved
        && !state->property("stagedGraph").toBool(), "save failure cannot claim a saved graph or discard its draft");
    expect(cleanBindings, "editor parts load and react without outer page ids or binding errors");
    return failures ? 1 : 0;
}

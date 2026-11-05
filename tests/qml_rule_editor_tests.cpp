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
        expect(contrast(QColor(colors.value("accentSoftText").toString()), QColor(colors.value("navigationSelected").toString())) >= 4.5,
            "selected navigation text contrasts with its soft background");
        for (const char* role : {"softButton", "softButtonHover", "softButtonPressed"})
            expect(contrast(QColor(colors.value("accentSoftText").toString()), QColor(colors.value(role).toString())) >= 4.5,
                "soft action labels contrast with normal, hover and pressed fills");
        for (const QString role : {QStringLiteral("success"), QStringLiteral("warning"), QStringLiteral("danger")})
            expect(contrast(QColor(colors.value(role).toString()), QColor(colors.value(role + "Surface").toString())) >= 4.5,
                "semantic status text remains readable on its corresponding background");
        const auto fileTypes = colors.value("fileTypes").toMap();
        const QColor switchOn(colors.value("switchTrackOn").toString()), switchOff(colors.value("switchTrackOff").toString());
        expect(contrast(switchOn, switchOff) >= 3.0
            && contrast(QColor(colors.value("switchThumbOn").toString()), switchOn) >= 3.0
            && contrast(QColor(colors.value("switchThumbOff").toString()), switchOff) >= 3.0,
            "switch states and thumb positions remain visually distinct in both themes");
        for (const auto& value : fileTypes) {
            const auto swatch = value.toMap();
            expect(contrast(QColor(swatch.value("foreground").toString()), QColor(swatch.value("background").toString())) >= 4.5,
                "file type labels contrast with their category tiles in both themes");
        }
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
    call(engine, canvas.get(), "openAddMenu", {50, 40});
    settle();
    // 반복 생성된 메뉴 항목을 팝업의 실제 화면 트리에서 찾는 함수.
    const auto findMenuItem = [&](auto&& self, QQuickItem* root, const QString& name) -> QQuickItem* {
        if (!root) return nullptr;
        if (root->objectName() == name) return root;
        for (auto* child : root->childItems()) if (auto* found = self(self, child, name)) return found;
        return nullptr;
    };
    auto* menuContent = addMenu ? addMenu->property("contentItem").value<QQuickItem*>() : nullptr;
    auto* conditionGroup = findMenuItem(findMenuItem, menuContent, "nodeAddGroup_0");
    auto* resultGroup = findMenuItem(findMenuItem, menuContent, "nodeAddGroup_1");
    auto* extensionEntry = findMenuItem(findMenuItem, menuContent, "addNode_extension");
    auto* destinationEntry = findMenuItem(findMenuItem, menuContent, "addNode_destination");
    expect(conditionGroup && resultGroup && extensionEntry && destinationEntry
        && !extensionEntry->isVisible() && !destinationEntry->isVisible(),
        "node menu initially shows categories with node lists collapsed");
    if (conditionGroup && resultGroup && extensionEntry && destinationEntry) {
        QMetaObject::invokeMethod(conditionGroup, "clicked");
        settle();
        expect(extensionEntry->isVisible() && !destinationEntry->isVisible(), "clicking a category expands its node list");
        QMetaObject::invokeMethod(resultGroup, "clicked");
        settle();
        expect(!extensionEntry->isVisible() && destinationEntry->isVisible(), "switching categories keeps the menu compact");
        QMetaObject::invokeMethod(resultGroup, "clicked");
        expect(!destinationEntry->isVisible(), "clicking the expanded category collapses it");
    }
    call(engine, canvas.get(), "dismissTransientUi");
    auto card = create(engine, "RuleNodeCard", {{"editor", QVariant::fromValue(state.get())},
        {"viewport", QVariant::fromValue(qobject_cast<QQuickItem*>(canvas.get()))}, {"modelData", list(state.get(), "blocks").first()}});
    if (!card) return 1;
    auto* outputPort = card->findChild<QObject*>("ruleOutputPort_1");
    if (!outputPort) return 1;
    state->setProperty("connectingFrom", 1);
    state->setProperty("draggingConnection", true);
    QMetaObject::invokeMethod(outputPort, "canceled");
    expect(state->property("connectingFrom").toInt() == -1 && !state->property("draggingConnection").toBool()
        && list(state.get(), "edges") == initialEdges, "lost pointer grab cancels only the temporary wire");
    state->setProperty("connectingFrom", 1);
    state->setProperty("draggingConnection", true);
    call(engine, canvas.get(), "dismissTransientUi");
    expect(state->property("connectingFrom").toInt() == -1 && !state->property("draggingConnection").toBool(),
        "leaving the canvas cancels an unfinished connection");

    QQmlComponent stub(&engine);
    stub.setData(R"(import QtQuick
        QtObject {
            property bool dirty: false
            property bool allowImport: true
            property bool allowSave: true
            property string message: "Blocked"
            property int saves: 0
            property int layouts: 0
            // 실제 파일시스템 없이 목적지 표시 문자열만 반환하는 대역 함수.
            function resolvedPath(path, relative) { return relative ? "D:/Test/" + path : path }
            signal rulesSaved()
            // 파일을 저장하지 않고 허용 여부만 반환하는 대역 함수.
            function importGraph(blocks, edges) { return allowImport }
            // 저장 완료 알림의 실제 연결 순서만 재현하는 대역 함수.
            function save() { ++saves; if (!allowSave) return false; rulesSaved(); return true }
            // 노드 위치 저장 요청을 세는 대역 함수.
            function saveGraphLayout(blocks, edges) { ++layouts; return true }
            // 파일을 읽지 않고 고정된 저장본을 반환하는 대역 함수.
            function graph() { return {valid: true, blocks: [{id: 1, type: "start", x: 70, y: 220, value: "Input"}], edges: [], nextId: 2, incompleteLeaves: 0} }
            // 화면 이동을 시험할 검사 결과를 반환하는 대역 함수.
            function inspectGraph(blocks, edges) { return {warnings: [{blockId: 1, reason: "Warning"}]} }
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
    // 최소 편집기 폭에서도 메뉴와 주요 명령이 잘리거나 겹치지 않는지 검사한다.
    auto toolbar = create(engine, "RuleEditorToolbar", {{"editor", QVariant::fromValue(state.get())},
        {"canvasHost", QVariant::fromValue(host.contentItem())}, {"width", 828}});
    auto inspector = create(engine, "RuleInspector", {{"editor", QVariant::fromValue(state.get())},
        {"destinations", QVariant::fromValue(controller.get())}, {"overlayHost", QVariant::fromValue(host.contentItem())},
        {"width", metrics->property("ruleInspectorWidth")}, {"height", 480}});
    if (!toolbar || !inspector) return 1;
    auto* toolbarItem = qobject_cast<QQuickItem*>(toolbar.get());
    toolbarItem->setParentItem(host.contentItem());
    qobject_cast<QQuickItem*>(inspector.get())->setParentItem(host.contentItem());
    for (bool dark : {false, true}) {
        toolbar->setProperty("dark", dark);
        inspector->setProperty("dark", dark);
        settle();
        auto* save = toolbar->findChild<QQuickItem*>("ruleSaveButton");
        auto* test = toolbar->findChild<QQuickItem*>("ruleTestButton");
        auto* quickAdd = toolbar->findChild<QQuickItem*>("ruleQuickAddButton");
        auto* view = toolbar->findChild<QQuickItem*>("ruleViewMenuButton");
        if (!save || !test || !quickAdd || !view) return 1;
        expect(test->mapToItem(toolbarItem, QPointF(test->width(), 0)).x() <= save->mapToItem(toolbarItem, QPointF()).x()
            && save->mapToItem(toolbarItem, QPointF(save->width(), 0)).x() <= toolbarItem->width()
            && view->mapToItem(toolbarItem, QPointF(view->width(), 0)).x() <= quickAdd->mapToItem(toolbarItem, QPointF()).x(),
            "compact editor menus and main actions fit the minimum content width in both themes");
    }
    auto* savedState = toolbar->findChild<QObject*>("ruleSaveState");
    toolbar->setProperty("hasUnsavedChanges", true);
    expect(savedState && savedState->property("text").toString().contains(QStringLiteral("변경")),
        "toolbar reflects unsaved changes without writing the document");
    call(engine, state.get(), "selectOnly", {2});
    auto* value = inspector->findChild<QQuickItem*>("ruleInspectorValue");
    expect(value && value->isVisible() && inspector->property("valueLabel").toString() == QStringLiteral("파일 크기"),
        "inspector labels the selected condition's actual input");
    call(engine, state.get(), "selectOnly", {1});
    expect(value && !value->isVisible() && inspector->property("selectionKind").toString() == QStringLiteral("시작점"),
        "the unique input node has guidance instead of an editable condition value");
    call(engine, state.get(), "addBlock", {"itemType", 150, 150});
    settle();
    auto* itemType = inspector->findChild<QQuickItem*>("ruleItemTypeCombo");
    expect(itemType && itemType->isVisible() && itemType->property("currentValue").toString() == "folder"
        && !value->isVisible() && inspector->property("valueLabel").toString() == QStringLiteral("대상 종류"),
        "item type condition uses a folder-default dropdown instead of free text");
    if (itemType) {
        itemType->setProperty("currentIndex", 0);
        QMetaObject::invokeMethod(itemType, "activated", Q_ARG(int, 0));
        const auto selected = state->property("selectedBlock").value<QJSValue>().toVariant().toMap();
        expect(selected.value("value").toString() == "file", "item type choice updates the selected graph node");
    }
    const auto beforeFlow = list(state.get(), "blocks").size();
    state->setProperty("zoom", 0.73);
    state->setProperty("panX", -185.0);
    state->setProperty("panY", 92.0);
    call(engine, state.get(), "addBlock", {"folderFlow", 150, 500});
    settle();
    expect(std::abs(state->property("zoom").toDouble() - 0.73) < 0.00001
        && state->property("panX").toDouble() == -185.0 && state->property("panY").toDouble() == 92.0,
        "adding a folder flow preserves the current zoom and camera position");
    const auto flowNodes = list(state.get(), "blocks");
    expect(flowNodes.size() == beforeFlow + 3, "one folder-flow action creates input, traversal and default call");
    const int flowStart = flowNodes[beforeFlow].toMap().value("id").toInt();
    expect(value && value->isVisible() && inspector->property("valueLabel").toString() == QStringLiteral("흐름 이름"),
        "folder flow name is editable in the inspector");
    call(engine, state.get(), "updateValue", {flowStart, "내 파일 따로 정리"});
    const auto regions = call(engine, state.get(), "flowRegions").toVariant().toList();
    expect(regions.size() == 2 && regions.last().toMap().value("name").toString() == QStringLiteral("내 파일 따로 정리"),
        "named frame reflects edits without creating another window");
    const auto edgeCount = list(state.get(), "edges").size();
    expect(!call(engine, state.get(), "connectBlocks", {1, flowStart}).toBool() && list(state.get(), "edges").size() == edgeCount,
        "folder flow cannot be wired into the base tree as a condition");
    call(engine, state.get(), "selectOnly", {flowStart + 1});
    expect(value && !value->isVisible(), "fixed traversal step has guidance instead of a meaningless value editor");
    auto* traversalScope = inspector->findChild<QObject*>("flowTraversalDepthCombo");
    auto* traversalDepth = inspector->findChild<QQuickItem*>("flowTraversalMaxDepth");
    expect(traversalScope && traversalDepth && !traversalDepth->isVisible(), "unlimited traversal hides the depth input");
    if (traversalScope && traversalDepth) {
        traversalScope->setProperty("currentIndex", 2);
        QMetaObject::invokeMethod(traversalScope, "activated", Q_ARG(int, 2));
        expect(traversalDepth->isVisible() && traversalDepth->property("value").toInt() == 1,
            "limited traversal opens the depth input at one level");
        traversalDepth->setProperty("value", 2);
        QMetaObject::invokeMethod(traversalDepth, "valueModified");
        auto selected = state->property("selectedBlock").value<QJSValue>().toVariant().toMap();
        expect(selected.value("maxDepth") == 2 && selected.value("recursive").toBool(), "depth edits update the graph");
        traversalScope->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(traversalScope, "activated", Q_ARG(int, 1));
        selected = state->property("selectedBlock").value<QJSValue>().toVariant().toMap();
        expect(!selected.contains("maxDepth") && !selected.value("recursive").toBool(),
            "immediate-only traversal clears the saved limit");
    }
    call(engine, state.get(), "removeSelectedBlock");
    expect(list(state.get(), "blocks").size() == beforeFlow + 2, "individual processing steps are editable rather than locked in a preset");
    call(engine, state.get(), "selectOnly", {flowStart});
    call(engine, state.get(), "removeSelectedBlock");
    expect(list(state.get(), "blocks").size() == beforeFlow, "deleting the flow input removes its complete region");
    call(engine, state.get(), "addBlock", {"classificationFlow", 150, 500});
    settle();
    expect(list(state.get(), "blocks").size() == beforeFlow + 2
        && std::abs(state->property("zoom").toDouble() - 0.73) < 0.00001
        && state->property("panX").toDouble() == -185.0 && state->property("panY").toDouble() == 92.0,
        "adding a separate classifier preserves the current zoom and camera position");
    call(engine, state.get(), "removeSelectedBlock");
    call(engine, state.get(), "addBlock", {"folderFlow", 150, 500});
    const int editableRoot = state->property("selectedId").toInt();
    call(engine, state.get(), "selectOnly", {editableRoot + 2});
    auto* callTarget = inspector->findChild<QQuickItem*>("flowCallTargetCombo");
    expect(callTarget && callTarget->isVisible() && callTarget->property("currentValue").toString().isEmpty(),
        "classifier call uses an explicit target dropdown instead of fixed base behavior");
    call(engine, state.get(), "updateProcessing", {editableRoot + 2, "targetFlow", "custom-classifier"});
    expect(state->property("selectedBlock").value<QJSValue>().toVariant().toMap().value("targetFlow") == "custom-classifier",
        "processing settings change the graph data");
    call(engine, state.get(), "removeSelectedBlock");
    call(engine, state.get(), "selectOnly", {editableRoot + 1});
    call(engine, state.get(), "addBlock", {"extension", 450, 500});
    const int customCondition = state->property("selectedId").toInt();
    expect(call(engine, state.get(), "connectBlocks", {editableRoot + 1, customCondition}).toBool(),
        "a traversal can connect to a custom condition");
    call(engine, state.get(), "addBlock", {"destination", 750, 500});
    const int customDestination = state->property("selectedId").toInt();
    expect(call(engine, state.get(), "connectBlocks", {customCondition, customDestination}).toBool(),
        "custom condition and destination can replace the preset call");
    call(engine, state.get(), "selectOnly", {editableRoot});
    call(engine, state.get(), "removeSelectedBlock");
    call(engine, state.get(), "addBlock", {"sourceFolder", 150, 500});
    settle();
    auto* sourceDepth = inspector->findChild<QQuickItem*>("sourceFolderDepthCombo");
    auto* sourceChoose = inspector->findChild<QQuickItem*>("sourceFolderChooseButton");
    expect(sourceDepth && sourceDepth->isVisible() && sourceChoose && sourceChoose->isVisible()
        && value && value->isVisible(), "source folder has path entry, folder picker and depth selector");
    if (sourceDepth) {
        sourceDepth->setProperty("currentIndex", 1);
        QMetaObject::invokeMethod(sourceDepth, "activated", Q_ARG(int, 1));
        expect(!state->property("selectedBlock").value<QJSValue>().toVariant().toMap().value("recursive").toBool(),
            "source depth selection updates the graph");
    }
    call(engine, state.get(), "addBlock", {"all", 600, 500});
    const int andId=state->property("selectedId").toInt();
    call(engine, state.get(), "addBlock", {"extension", 150, 500});
    const int firstOperand=state->property("selectedId").toInt();
    call(engine, state.get(), "addBlock", {"name", 150, 700});
    const int secondOperand=state->property("selectedId").toInt();
    expect(call(engine,state.get(),"connectBlocks",{firstOperand,andId,"predicate"}).toBool()
        && call(engine,state.get(),"connectBlocks",{secondOperand,andId,"predicate"}).toBool(),"separate condition nodes connect to the logical input");
    expect(!call(engine,state.get(),"connectBlocks",{1,firstOperand}).toBool(),"operand cannot also receive execution flow");
    expect(call(engine,state.get(),"connectBlocks",{1,andId}).toBool(),"logical node has an independent execution input");
    state->setProperty("selectedIds", engine.toScriptValue(QVariantList{andId,firstOperand,secondOperand}).toVariant());
    call(engine,state.get(),"copySelectedBlocks");
    const int beforePaste=list(state.get(),"edges").size();
    call(engine,state.get(),"pasteCopiedBlocks");
    const auto pastedEdges=list(state.get(),"edges");
    expect(pastedEdges.size()==beforePaste+2 && pastedEdges.last().toMap().value("kind")=="predicate",
        "copy and paste preserves condition edge types");
    const auto logicalBlock=call(engine,state.get(),"blockForId",{andId}).toVariant();
    auto logicalCard=create(engine,"RuleNodeCard",{{"editor",QVariant::fromValue(state.get())},
        {"viewport",QVariant::fromValue(qobject_cast<QQuickItem*>(canvas.get()))},{"modelData",logicalBlock}});
    expect(logicalCard && logicalCard->property("width").toDouble()<state->property("blockWidth").toDouble()
        && logicalCard->property("height").toDouble()<state->property("blockHeight").toDouble(),"AND node is smaller than ordinary condition cards");
    expect(logicalCard && logicalCard->findChild<QObject*>(QString("rulePredicatePort_%1").arg(andId)),"logical node exposes a separate condition socket");
    auto inspectionPage = create(engine, "RuleEditorPage", {
        {"controller", QVariant::fromValue(controller.get())}, {"destinations", QVariant::fromValue(controller.get())},
        {"guideService", QVariant::fromValue(controller.get())}, {"width", 828}, {"height", 600}
    });
    if (!inspectionPage) return 1;
    qobject_cast<QQuickItem*>(inspectionPage.get())->setParentItem(host.contentItem());
    settle();
    inspectionPage->setProperty("zoom", 1.25);
    auto* inspectAction = inspectionPage->findChild<QObject*>("ruleInspectAction");
    auto* inspection = inspectionPage->findChild<QObject*>("ruleInspectionDialog");
    expect(inspectAction && inspection, "inspection command and results dialog are present");
    if (inspectAction && inspection) {
        QMetaObject::invokeMethod(inspectAction, "clicked");
        settle();
        auto* content = inspection->property("contentItem").value<QQuickItem*>();
        auto* row = findMenuItem(findMenuItem, content, "ruleInspectionRow");
        expect(inspection->property("opened").toBool() && row && row->isVisible(), "inspection opens with a navigable warning");
        if (row) {
            QMetaObject::invokeMethod(row, "clicked");
            settle();
            const auto selected = inspectionPage->property("selectedBlock").value<QJSValue>().toVariant().toMap();
            expect(!inspection->property("visible").toBool() && selected.value("id") == 1
                && inspectionPage->property("zoom").toDouble() == 1.25, "warning selects its node and preserves zoom");
        }
    }
    auto testPanel = create(engine, "RuleTestPanel", {
        {"editor", QVariant::fromValue(state.get())}, {"controller", QVariant::fromValue(controller.get())},
        {"parent", QVariant::fromValue(host.contentItem())}
    });
    if (!testPanel) return 1;
    const QVariantMap conditionResult{{"ok", true}, {"outcome", "unclassified"}, {"path", QVariantList{}},
        {"conditions", QVariantList{QVariantMap{{"blockId", andId}, {"depth", 0}, {"status", "notMatched"}}}}};
    call(engine, state.get(), "applyTestResult", {conditionResult});
    QMetaObject::invokeMethod(testPanel.get(), "open");
    settle();
    auto* resultButton = testPanel->findChild<QQuickItem*>("ruleConditionResultsButton");
    auto* resultDialog = testPanel->findChild<QObject*>("ruleConditionResultsDialog");
    expect(resultButton && resultButton->isVisible() && resultDialog, "tested conditions expose the results action");
    if (resultButton && resultDialog) {
        QMetaObject::invokeMethod(resultButton, "clicked");
        settle();
        auto* content = resultDialog->property("contentItem").value<QQuickItem*>();
        auto* row = findMenuItem(findMenuItem, content, "ruleConditionResultRow");
        expect(resultDialog->property("opened").toBool() && row && row->isVisible(), "condition report lists the tested node");
        if (row) {
            QMetaObject::invokeMethod(row, "clicked");
            settle();
            expect(!resultDialog->property("visible").toBool() && !testPanel->property("visible").toBool()
                && state->property("selectedId").toInt() == andId, "condition result reveals its canvas node");
        }
    }
    expect(cleanBindings, "editor parts load and react without outer page ids or binding errors");
    return failures ? 1 : 0;
}

#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPointer>
#include <QJSValue>
#include <memory>
#include <iostream>

namespace {
int failures = 0;

// 화면 경계가 지켜지지 않으면 설명과 함께 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

// 팝업 열기·닫기의 실제 이벤트 반영을 기다리는 함수.
void settle() {
    QEventLoop events;
    QTimer::singleShot(160, &events, &QEventLoop::quit);
    events.exec();
}

// 앱의 전역 컨텍스트 없이 로컬 QML을 명시적인 입력만으로 생성하는 함수.
std::unique_ptr<QObject> create(QQmlEngine& engine, const char* name, const QVariantMap& properties = {}) {
    QQmlComponent component(&engine, QUrl::fromLocalFile(QString::fromUtf8(NOOKBIT_SOURCE_DIR)
        + "/src/presentation/qml/" + QString::fromLatin1(name) + ".qml"));
    std::unique_ptr<QObject> result(component.createWithInitialProperties(properties));
    if (!result) { ++failures; std::cerr << component.errorString().toStdString(); }
    return result;
}

// 파일 작업을 하지 않고 호출 여부와 상태만 보관하는 대역을 생성하는 함수.
std::unique_ptr<QObject> fake(QQmlEngine& engine, const QByteArray& source) {
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nQtObject {\n" + source + "\n}", QUrl());
    std::unique_ptr<QObject> result(component.create());
    if (!result) { ++failures; std::cerr << component.errorString().toStdString(); }
    return result;
}

// 등록된 화면이 전역 기능 이름 없이 입력·테마·열기 요청을 받는지 검사하는 함수.
void testRegisteredPageHost(QQmlEngine& engine, QQuickWindow& window) {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "registered-page fixture"); return; }
    const auto source = directory.filePath("Probe.qml");
    QFile file(source);
    if (!file.open(QIODevice::WriteOnly)) { expect(false, "create registered-page probe"); return; }
    file.write(R"(import QtQuick
Item {
    objectName: "registeredPageProbe"
    required property var service
    required property var guideService
    property bool dark: false
    property int draft: 0
    signal openRequested()
    signal closeRequested()
})");
    file.close();
    auto service = fake(engine, "property string token: 'feature-input'");
    if (!service) return;
    const QVariantMap registration{{"page", 37}, {"source", QUrl::fromLocalFile(source)},
        {"properties", QVariantMap{{"service", QVariant::fromValue(service.get())}}}};
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
RegisteredPageHost {
    property int openedRoute: 0
    property int closedRoute: 0
    onPageRequested: function(page) { openedRoute = page }
    onCloseRequested: function(page) { closedRoute = page }
})", QUrl::fromLocalFile(QStringLiteral(NOOKBIT_SOURCE_DIR "/src/presentation/qml/RegisteredHostHarness.qml")));
    std::unique_ptr<QObject> owner(component.createWithInitialProperties({{"registration", registration},
        {"guideService", QVariant::fromValue(service.get())}, {"width", 500}, {"height", 400}}));
    if (!owner) { expect(false, "create registered-page host"); std::cerr << component.errorString().toStdString(); return; }
    qobject_cast<QQuickItem*>(owner.get())->setParentItem(window.contentItem());
    QPointer<QObject> probe = owner->findChild<QObject*>("registeredPageProbe");
    expect(probe && !probe->property("visible").toBool(), "registered page loads hidden before selection");
    if (!probe) return;
    expect(probe->property("service").value<QObject*>() == service.get()
        && probe->property("guideService").value<QObject*>() == service.get(), "feature and host inputs injected explicitly");
    owner->setProperty("selected", true);
    probe->setProperty("draft", 42);
    owner->setProperty("dark", true);
    expect(probe->property("visible").toBool() && probe->property("dark").toBool(), "selection and live theme reach registered page");
    QMetaObject::invokeMethod(probe, "openRequested");
    QMetaObject::invokeMethod(probe, "closeRequested");
    expect(owner->property("openedRoute").toInt() == 37 && owner->property("closedRoute").toInt() == 37,
        "page requests are routed through its registration instead of a feature-specific host branch");
    owner->setProperty("selected", false);
    owner->setProperty("selected", true);
    expect(probe && probe->property("draft").toInt() == 42, "ordinary navigation retains the page draft");
    owner->setProperty("registration", QVariantMap{});
    settle();
    expect(probe.isNull() && owner->property("item").value<QObject*>() == nullptr,
        "missing registration releases its loaded page");

    auto home = create(engine, "CompactHome", {{"width", 380}, {"height", 660}});
    if (!home) return;
    expect(home->property("navigationEntries").value<QJSValue>().toVariant().toList().size() == 5,
        "unregistered companion has no hardcoded home menu entry");
    auto menuEntry = registration;
    menuEntry.insert("label", "Companion"); menuEntry.insert("icon", "companion");
    menuEntry.insert("section", "Companion"); menuEntry.insert("order", 40);
    home->setProperty("registeredPages", QVariantList{menuEntry});
    const auto entries = home->property("navigationEntries").value<QJSValue>().toVariant().toList();
    expect(entries.size() == 6 && entries[3].toMap().value("page").toInt() == 37,
        "registered menu is placed between file tools and app settings");
    home->setProperty("registeredPages", QVariantList{});
    expect(home->property("navigationEntries").value<QJSValue>().toVariant().toList().size() == 5,
        "removing registration removes its home entry");
}
}

// 입력 초안의 독립성과 페이지 소유 확인창의 종료를 앱 조립부 없이 검사하는 함수.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    auto metrics = create(engine, "UiMetrics");
    auto theme = create(engine, "UiTheme");
    if (!metrics || !theme) return 1;
    engine.rootContext()->setContextProperty("UiMetrics", metrics.get());
    engine.rootContext()->setContextProperty("UiTheme", theme.get());
    bool cleanBindings = true;
    // 분리한 페이지가 숨겨진 전역 이름이나 잘못된 입력을 참조하면 실패시키는 함수.
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&](const QList<QQmlError>& errors) {
        cleanBindings = false;
        for (const auto& error : errors) std::cerr << error.toString().toStdString() << '\n';
    });
    auto watch = fake(engine, R"(
        id: watchStub
        property string folder: "D:/Watch"
        property bool enabled: true
        property bool recursive: false
        property int rescanIntervalMinutes: 45
        property bool combineAll: false
        property bool countEnabled: true
        property int minimumFileCount: 20
        property bool sizeEnabled: true
        property int minimumTotalSize: 100
        property string totalSizeUnit: "MB"
        property bool ageEnabled: true
        property int minimumAgeMinutes: 60
        property string excludePatterns: "*.partial"
        property string statusMessage: ""
        property var lastEvaluation: ({})
        signal settingsChanged()
        property bool rejectSave: false
        property int saveCalls: 0
        // 저장 성공 알림과 실패 시 무변경만 재현하는 대역 함수.
        function saveSettings(folder, enabled, recursive, interval, all, count, minimumCount, size, minimumSize, age, minimumAge, patterns, unit) {
            ++saveCalls
            if (rejectSave) return
            watchStub.rescanIntervalMinutes = interval
            watchStub.excludePatterns = patterns
            settingsChanged()
        }
    )");
    QQuickWindow host;
    host.resize(960, 760);
    host.show();
    testRegisteredPageHost(engine, host);
    if (!watch) return 1;
    const QVariantMap watchInputs{{"controller", QVariant::fromValue(watch.get())}, {"visible", false},
        {"width", 900}, {"height", 700}};
    auto first = create(engine, "WatchSettingsPage", watchInputs);
    auto second = create(engine, "WatchSettingsPage", watchInputs);
    if (!first || !second) return 1;
    for (auto* page : {first.get(), second.get()}) {
        qobject_cast<QQuickItem*>(page)->setParentItem(host.contentItem());
        page->setProperty("visible", true);
    }
    auto* a = first->findChild<QObject*>("watchIntervalSpin");
    auto* b = second->findChild<QObject*>("watchIntervalSpin");
    if (!a || !b) return 1;
    expect(a->property("value").toInt() == 45 && b->property("value").toInt() == 45,
        "each watch page loads the injected saved settings");
    a->setProperty("value", 37);
    expect(b->property("value").toInt() == 45 && watch->property("rescanIntervalMinutes").toInt() == 45,
        "editing one draft cannot mutate another page or saved settings");
    first->setProperty("visible", false);
    watch->setProperty("rescanIntervalMinutes", 72);
    first->setProperty("visible", true);
    expect(a->property("value").toInt() == 72 && b->property("value").toInt() == 45,
        "reopening reloads only that page draft");
    first->setProperty("visible", false);
    second->setProperty("visible", false);

    auto history = fake(engine, R"(
        property var historyEntries: []
        property var historySteps: []
        property string historyMessage: ""
        property string selectedHistoryPlanId: "plan"
        property bool canUndoSelectedHistory: true
        property int selectedHistoryFileCount: 1
        property int undoCalls: 0
        function undoSelectedHistory() { ++undoCalls }
    )");
    auto recovery = fake(engine, R"(
        property var recoveryCases: []
        property string recoveryMessage: ""
        property bool recoveryBusy: false
        property bool awaitingRecoveryConfirmation: false
        property string recoveryConfirmationText: "Confirm"
        property int cancelCalls: 0
        property int confirmCalls: 0
        signal recoveryConfirmationChanged()
        function cancelRecoveryChoice() { ++cancelCalls; awaitingRecoveryConfirmation = false; recoveryConfirmationChanged() }
        function confirmRecoveryChoice() { ++confirmCalls }
    )");
    if (!history || !recovery) return 1;
    // Controller와 확인창의 표시 부모만 각 페이지에 명시적으로 전달하는 함수.
    const auto pageInputs = [&](QObject* controller) {
        return QVariantMap{{"controller", QVariant::fromValue(controller)},
            {"overlayHost", QVariant::fromValue(host.contentItem())}, {"visible", false}, {"width", 900}, {"height", 700}};
    };
    auto historyPage = create(engine, "HistoryPage", pageInputs(history.get()));
    auto recoveryPage = create(engine, "RecoveryPage", pageInputs(recovery.get()));
    if (!historyPage || !recoveryPage) return 1;
    for (auto* page : {historyPage.get(), recoveryPage.get()})
        qobject_cast<QQuickItem*>(page)->setParentItem(host.contentItem());
    auto* undo = historyPage->findChild<QObject*>("historyUndoConfirmation");
    auto* confirm = recoveryPage->findChild<QObject*>("recoveryConfirmDialog");
    if (!undo || !confirm) return 1;
    historyPage->setProperty("visible", true);
    QMetaObject::invokeMethod(undo, "open");
    settle();
    expect(undo->property("visible").toBool(), "undo confirmation opens over the injected window");
    historyPage->setProperty("visible", false);
    settle();
    expect(!undo->property("visible").toBool() && history->property("undoCalls").toInt() == 0,
        "leaving history closes its overlay without running undo");
    recoveryPage->setProperty("visible", true);
    recovery->setProperty("awaitingRecoveryConfirmation", true);
    QMetaObject::invokeMethod(recovery.get(), "recoveryConfirmationChanged");
    settle();
    expect(confirm->property("visible").toBool(), "recovery confirmation follows controller state");
    recoveryPage->setProperty("visible", false);
    settle();
    expect(!confirm->property("visible").toBool() && recovery->property("cancelCalls").toInt() == 1
        && recovery->property("confirmCalls").toInt() == 0 && !recovery->property("awaitingRecoveryConfirmation").toBool(),
        "leaving recovery cancels approval and cannot start file work");

    // 홈이 전달받은 값만으로 표시되고 상태 소유자를 변경하지 않는지 검사한다.
    auto home = create(engine, "CompactHome", {{"width", 380}, {"height", 420}, {"pendingCount", 5},
        {"watchEnabled", true}, {"watchFolder", QStringLiteral("D:\\Downloads\\")}});
    if (!home) return 1;
    auto* homeItem = qobject_cast<QQuickItem*>(home.get());
    homeItem->setParentItem(host.contentItem());
    settle();
    // Repeater가 만든 메뉴까지 실제 화면 트리에서 찾는 함수.
    const auto find = [](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems()) if (auto* found = self(self, child, name)) return found;
        return nullptr;
    };
    auto* card = find(find, homeItem, QStringLiteral("homeStatusCard"));
    auto* action = find(find, homeItem, QStringLiteral("homeStatusAction"));
    auto* summary = find(find, homeItem, QStringLiteral("homePendingSummary"));
    auto* watchSummary = find(find, homeItem, QStringLiteral("homeWatchSummary"));
    auto* filesButton = find(find, homeItem, QStringLiteral("homeNavigation7"));
    auto* badge = filesButton ? find(find, filesButton, QStringLiteral("homeNavigationBadge")) : nullptr;
    if (!card || !action || !summary || !watchSummary || !filesButton || !badge) return 1;
    expect(summary->property("text").toString().contains("5") && badge->isVisible()
        && filesButton->property("badgeText").toString() == "5"
        && card->property("folderName").toString() == "Downloads",
        "home summary and badge reflect injected values and normalize Windows folder paths");
    home->setProperty("pendingCount", 0);
    home->setProperty("watchEnabled", false);
    QCoreApplication::processEvents();
    expect(!badge->isVisible() && summary->property("text").toString().contains(QStringLiteral("없어요"))
        && watchSummary->property("text").toString().contains(QStringLiteral("꺼짐")),
        "empty queue and disabled watching have explicit home states");
    home->setProperty("pendingCount", 1000000);
    settle();
    auto* menuLabel = find(find, filesButton, QStringLiteral("homeMenuLabel"));
    expect(menuLabel && menuLabel->mapToItem(filesButton, QPointF(menuLabel->width(), 0)).x()
        <= badge->mapToItem(filesButton, QPointF()).x(), "large file counts reserve space instead of covering menu labels");
    QQmlComponent observerComponent(&engine);
    observerComponent.setData(R"(import QtQuick
        Connections {
            required property var home
            property int lastPage: -1
            target: home
            // 홈의 이동 요청만 기록하고 파일 작업은 실행하지 않는 함수.
            function onPageRequested(page) { lastPage = page }
        })", QUrl());
    std::unique_ptr<QObject> observer(observerComponent.createWithInitialProperties({{"home", QVariant::fromValue(home.get())}}));
    if (!observer) { std::cerr << observerComponent.errorString().toStdString(); return 1; }
    QMetaObject::invokeMethod(action, "clicked");
    expect(observer->property("lastPage").toInt() == 7 && action->height() == metrics->property("controlHeight").toInt(),
        "home action only requests the sorting page and uses the shared control height");
    home->setProperty("navigationEnabled", false);
    auto* titleDrag = find(find, homeItem, QStringLiteral("homeWindowDragArea"));
    expect(!action->isEnabled() && titleDrag && titleDrag->isEnabled(), "busy navigation lock preserves title dragging");
    home->setProperty("navigationEnabled", true);
    auto* scroll = find(find, homeItem, QStringLiteral("homeNavigationMenu"));
    auto* flickable = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
    auto* lastMenu = find(find, homeItem, QStringLiteral("homeNavigation11"));
    if (!flickable || !lastMenu) return 1;
    const auto contentHeight = flickable->property("contentHeight").toDouble();
    expect(contentHeight > flickable->height(), "short home windows keep card and all menus in one scroll area");
    flickable->setProperty("contentY", contentHeight - flickable->height());
    settle();
    expect(lastMenu->mapToItem(flickable, QPointF()).y() >= -1
        && lastMenu->mapToItem(flickable, QPointF(0, lastMenu->height())).y() <= flickable->height() + 1,
        "the final navigation entry remains reachable at minimum window height");

    // 공유 아틀라스와 비중앙 피벗·미러가 있어도 정지 미리보기를 상자 안에 맞추는 검사.
    QTemporaryDir images;
    if (!images.isValid()) return 1;
    QImage atlas(128, 64, QImage::Format_ARGB32);
    atlas.fill(Qt::red);
    const auto imagePath = images.filePath(QStringLiteral("home-atlas.png"));
    if (!atlas.save(imagePath)) return 1;
    const QVariantMap step{{"frameId", "first"}, {"source", QUrl::fromLocalFile(imagePath)},
        {"imageWidth", 128}, {"imageHeight", 64}, {"pixelsPerUnit", 1}, {"durationMs", 100},
        {"region", QVariantMap{{"x", 64}, {"y", 0}, {"width", 64}, {"height", 64}}},
        {"pivot", QVariantMap{{"x", 15}, {"y", 40}}}};
    home->setProperty("companionDescriptor", QVariantMap{{"steps", QVariantList{step, step}},
        {"mirrorX", true}, {"loop", true}, {"filtering", "linear"}});
    auto* sprite = home->findChild<QQuickItem*>(QStringLiteral("homeCompanionSprite"));
    if (!sprite) return 1;
    QElapsedTimer deadline;
    deadline.start();
    while (!sprite->property("ready").toBool() && deadline.elapsed() < 2000) settle();
    auto* crop = sprite->findChild<QQuickItem*>(QStringLiteral("characterFrameRegion"));
    auto* timer = sprite->findChild<QObject*>(QStringLiteral("characterSpriteTimer"));
    const auto pictureBounds = crop ? crop->mapRectToItem(sprite, QRectF(QPointF(), crop->size())) : QRectF();
    expect(sprite->property("ready").toBool() && timer && !timer->property("running").toBool()
        && pictureBounds.left() >= -0.01 && pictureBounds.top() >= -0.01
        && pictureBounds.right() <= sprite->width() + 0.01 && pictureBounds.bottom() <= sprite->height() + 0.01,
        "home portrait reuses atlas rendering, fits mirrored arbitrary pivots and never runs an animation timer");
    // 최소 창에서도 목록과 하단 동작이 겹치지 않고 같은 버튼 규격을 유지하는지 검사한다.
    QQmlComponent pendingComponent(&engine);
    pendingComponent.setData(R"(import QtQml.Models
        ListModel { property int unavailableCount: 0; property string storageNotice: "" })", QUrl());
    std::unique_ptr<QObject> pending(pendingComponent.create());
    auto sortController = fake(engine, R"(
        property string activeRuleLabel: "A long imported sorting rule document name.json"
        property string defaultDestinationRoot: "D:/NookBitTest"
        property string message: ""
        property bool canUndo: false
        property bool awaitingConfirmation: false
        function clear() { message = "" }
    )");
    if (!pending || !sortController) return 1;
    auto input = create(engine, "FileSortInput", {{"controller", QVariant::fromValue(sortController.get())},
        {"pending", QVariant::fromValue(pending.get())}, {"recovery", QVariant::fromValue(recovery.get())},
        {"width", 640}, {"height", 478}});
    if (!input) return 1;
    auto* inputItem = qobject_cast<QQuickItem*>(input.get());
    inputItem->setParentItem(host.contentItem());
    for (bool dark : {false, true}) {
        input->setProperty("dark", dark);
        for (int width : {640, 720, 1100}) {
            inputItem->setWidth(width);
            settle();
            auto* bar = find(find, inputItem, "fileSortActionBar");
            auto* queue = find(find, inputItem, "pendingQueueCard");
            auto* previewAction = find(find, inputItem, "compactPreviewButton");
            auto* sortAction = find(find, inputItem, "compactSortButton");
            auto* empty = find(find, inputItem, "pendingEmptyState");
            if (!bar || !queue || !previewAction || !sortAction || !empty) return 1;
            expect(queue->mapToItem(inputItem, QPointF(0, queue->height())).y() <= bar->mapToItem(inputItem, QPointF()).y()
                && bar->mapToItem(inputItem, QPointF(0, bar->height())).y() <= inputItem->height()
                && sortAction->mapToItem(inputItem, QPointF(sortAction->width(), 0)).x() <= inputItem->width(),
                "sorting queue and fixed footer fit the minimum height across window widths");
            expect(previewAction->width() == sortAction->width() && previewAction->height() == sortAction->height()
                && empty->isVisible() && !previewAction->isEnabled() && !sortAction->isEnabled(),
                "empty queues show guidance and disable equally sized sorting actions in both themes");
        }
    }
    auto stepper = create(engine, "FileSortStepper", {{"workflowStep", 4}});
    expect(stepper && stepper->property("currentStep").toInt() == 3,
        "execution and completion share the last informational step without adding a new workflow state");
    // 되돌림 요약은 번역된 상태 문구가 아니라 전달된 실제 상태 플래그로 집계하는 검사.
    const QVariantList historyFiles{
        QVariantMap{{"fileName", "report.pdf"}, {"source", "C:/Downloads/report.pdf"}, {"destination", "D:/Archive/report.pdf"},
            {"status", "localized state"}, {"statusTone", "success"}, {"applied", true}, {"rolledBack", false}},
        QVariantMap{{"fileName", "photo.png"}, {"source", "C:/Downloads/photo.png"}, {"destination", "D:/Pictures/photo.png"},
            {"status", "another localized state"}, {"statusTone", "success"}, {"applied", false}, {"rolledBack", true}}};
    auto historyDetail = create(engine, "HistoryDetail", {{"width", 559}, {"height", 480}, {"steps", historyFiles},
        {"entry", QVariantMap{{"time", "2026-10-27 21:21"}, {"status", "Completed"}, {"statusTone", "success"}, {"fileCount", 2}}},
        {"canUndo", true}, {"undoCount", 1}});
    if (!historyDetail) return 1;
    auto* detailItem = qobject_cast<QQuickItem*>(historyDetail.get());
    detailItem->setParentItem(host.contentItem());
    for (bool dark : {false, true}) {
        historyDetail->setProperty("dark", dark);
        settle();
        auto* undoAction = find(find, detailItem, "historyUndoButton");
        auto* moved = find(find, detailItem, "historyAppliedSummary");
        auto* restored = find(find, detailItem, "historyRolledBackSummary");
        auto* documentBadge = find(find, detailItem, "historyFileType_report.pdf");
        auto* imageBadge = find(find, detailItem, "historyFileType_photo.png");
        expect(documentBadge && imageBadge && documentBadge->property("category").toString() == "document"
            && imageBadge->property("category").toString() == "image"
            && documentBadge->property("color") != imageBadge->property("color"),
            "history uses distinct document and image tiles in both themes");
        expect(moved && restored && moved->property("count").toInt() == 1 && restored->property("count").toInt() == 1,
            "history counts distinguish current moves from restored files independently of translated labels");
        expect(undoAction && undoAction->isEnabled()
            && undoAction->mapToItem(detailItem, QPointF(undoAction->width(), undoAction->height())).x() <= detailItem->width()
            && undoAction->mapToItem(detailItem, QPointF(undoAction->width(), undoAction->height())).y() <= detailItem->height(),
            "history undo stays reachable within the minimum detail size in both themes");
    }
    historyDetail->setProperty("busy", true);
    expect(!find(find, detailItem, "historyUndoButton")->isEnabled(), "history cannot issue a second undo while file work is busy");
    // 팩 선택 잠금과 개인 크기 표시가 미리보기 UI와 독립적으로 유지되는지 검사한다.
    auto companionSettings = fake(engine, R"(
        property var packs: [{key: "nmoo", label: "Nmoo - Idle & Walk · 0.3.1"}]
        property string selectedKey: "nmoo"
        property bool busy: false
        property real userScale: 0.5
        property string status: ""
    )");
    auto placement = fake(engine, "property bool shown: true");
    auto motion = fake(engine, "property bool enabled: true");
    if (!companionSettings || !placement || !motion) return 1;
    auto companionPanel = create(engine, "CompanionSettingsPanel", {{"settings", QVariant::fromValue(companionSettings.get())},
        {"placement", QVariant::fromValue(placement.get())}, {"motion", QVariant::fromValue(motion.get())},
        {"canWalk", true}, {"width", 320}, {"height", 600}});
    if (!companionPanel) return 1;
    auto* panelItem = qobject_cast<QQuickItem*>(companionPanel.get());
    panelItem->setParentItem(host.contentItem());
    auto* pack = find(find, panelItem, "companionPackChoice");
    auto* scale = find(find, panelItem, "companionDisplayScale");
    if (!pack || !scale) return 1;
    for (bool dark : {false, true}) {
        companionPanel->setProperty("dark", dark);
        settle();
        expect(pack->property("currentIndex").toInt() == 0 && pack->isEnabled()
            && scale->property("value").toDouble() == 0.5,
            "companion panel reads the applied pack and desktop scale in both themes");
        companionPanel->setProperty("dirty", true);
        expect(!pack->isEnabled(), "unsaved frame edits prevent replacing the applied pack");
        companionPanel->setProperty("dirty", false);
    }
    // 설정 카드가 작은 창에서도 같은 폭으로 놓이고 기존 설정 창구에만 값을 전달하는 검사.
    auto uiPreferences = fake(engine, "property bool darkMode: false");
    auto destinationPreferences = fake(engine, "property var allowedRoots: []");
    auto sortPreferences = fake(engine, "property string collisionPolicy: \"rename\"");
    if (!uiPreferences || !destinationPreferences || !sortPreferences) return 1;
    auto settingsPage = create(engine, "AppSettingsPage", {{"settings", QVariant::fromValue(uiPreferences.get())},
        {"destinations", QVariant::fromValue(destinationPreferences.get())}, {"sorting", QVariant::fromValue(sortPreferences.get())},
        {"watch", QVariant::fromValue(watch.get())}, {"width", 500}, {"height", 520}});
    if (!settingsPage) return 1;
    auto* settingsItem = qobject_cast<QQuickItem*>(settingsPage.get());
    settingsItem->setParentItem(host.contentItem());
    for (bool dark : {false, true}) {
        settingsPage->setProperty("dark", dark);
        settle();
        for (int width : {500, 720}) {
            settingsItem->setWidth(width);
            settle();
            auto* themeGroup = find(find, settingsItem, "themeChoiceGroup");
            auto* themeCard = themeGroup ? themeGroup->parentItem() : nullptr;
            while (themeCard && themeCard->objectName() != QStringLiteral("settingsSectionCard")) themeCard = themeCard->parentItem();
            expect(themeGroup && themeCard
                && qAbs(themeGroup->mapToItem(themeCard, QPointF(themeGroup->width(), 0)).x()
                    - (themeCard->width() - metrics->property("spaceSection").toInt())) <= 1,
                "theme choices remain aligned to the actual card edge at narrow and wide widths");
        }
        settingsItem->setWidth(500);
        settle();
        auto* rename = find(find, settingsItem, "collisionPolicy_rename");
        auto* skip = find(find, settingsItem, "collisionPolicy_skip");
        auto* abort = find(find, settingsItem, "collisionPolicy_abort");
        if (!rename || !skip || !abort) return 1;
        expect(std::abs(rename->width() - skip->width()) <= 1 && std::abs(skip->width() - abort->width()) <= 1
            && abort->mapToItem(settingsItem, QPointF(abort->width(), 0)).x() <= settingsItem->width(),
            "collision choices have equal widths and fit a narrow settings page in both themes");
        QMetaObject::invokeMethod(skip, "clicked");
        expect(sortPreferences->property("collisionPolicy").toString() == "skip", "collision choice updates the injected setting");
        auto* darkChoice = find(find, settingsItem, "globalDarkModeSwitch");
        auto* lightChoice = find(find, settingsItem, "globalLightModeButton");
        if (!darkChoice || !lightChoice) return 1;
        QMetaObject::invokeMethod(darkChoice, "clicked");
        expect(uiPreferences->property("darkMode").toBool(), "dark choice explicitly selects dark mode");
        QMetaObject::invokeMethod(lightChoice, "clicked");
        expect(!uiPreferences->property("darkMode").toBool(), "light choice explicitly selects light mode");
    }
    // 설정과 감시를 같은 크기에서 왕복하고, 저장 실패가 초안을 지우지 않는지 검사한다.
    auto flow = create(engine, "SettingsWorkflow", {{"settings", QVariant::fromValue(uiPreferences.get())},
        {"destinations", QVariant::fromValue(destinationPreferences.get())}, {"sorting", QVariant::fromValue(sortPreferences.get())},
        {"watch", QVariant::fromValue(watch.get())}, {"active", true}, {"width", 500}, {"height", 520}});
    if (!flow) return 1;
    auto* flowItem = qobject_cast<QQuickItem*>(flow.get());
    flowItem->setParentItem(host.contentItem());
    flow->setProperty("watchOpen", true);
    settle(); settle();
    auto* watchPage = find(find, flowItem, "watchSettingsDialog");
    auto* watchInput = find(find, flowItem, "watchIntervalSpin");
    auto* patternInput = find(find, flowItem, "watchPatternInput");
    if (!watchPage || !watchInput || !patternInput) return 1;
    expect(watchPage->isVisible() && qAbs(watchPage->x()) < 1 && !flow->property("transitioning").toBool()
        && flowItem->size() == QSizeF(500, 520), "watch settings slide into the same viewport without resizing it");
    watchInput->setProperty("value", 17);
    patternInput->setProperty("text", "*.download");
    watch->setProperty("rejectSave", true);
    QMetaObject::invokeMethod(watchPage, "save");
    expect(watchPage->property("changeCount").toInt() > 0 && watch->property("rescanIntervalMinutes").toInt() != 17,
        "failed watch saves preserve both input changes and committed pending patterns");
    watch->setProperty("rejectSave", false);
    QMetaObject::invokeMethod(watchPage, "save");
    expect(watchPage->property("changeCount").toInt() == 0 && watch->property("rescanIntervalMinutes").toInt() == 17
        && watch->property("excludePatterns").toString().contains("*.download"),
        "successful saves include the last typed pattern and reset the draft baseline");
    flow->setProperty("watchOpen", false);
    settle(); settle();
    auto* returnedSettings = find(find, flowItem, "settingsPage");
    expect(returnedSettings && returnedSettings->isVisible() && qAbs(returnedSettings->x()) < 1
        && !watchPage->isVisible() && flowItem->size() == QSizeF(500, 520), "back navigation slides to settings at the same size");
    expect(cleanBindings, "pages must load without application globals or binding errors");
    return failures ? 1 : 0;
}

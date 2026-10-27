#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QEventLoop>
#include <QTimer>
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
    )");
    QQuickWindow host;
    host.resize(960, 760);
    host.show();
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
    expect(cleanBindings, "pages must load without application globals or binding errors");
    return failures ? 1 : 0;
}

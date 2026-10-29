#include "app/file_organizer_runtime.hpp"
#include "app/companion_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include <nookbit/application/pending_queue.hpp>
#include "presentation/pending_files_model.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "presentation/destination_settings_controller.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/character_window_controller.hpp"
#include "presentation/companion_edit_controller.hpp"
#include "presentation/companion_interaction_controller.hpp"
#include "presentation/companion_motion_controller.hpp"
#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>
#include <QSemaphore>
#include <QThread>
#include <atomic>
#include <iostream>

namespace {
int failures{};
// 예상과 다른 수명·설정·파일 상태를 기록하는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 화면을 띄우지 않고 실행기와 결과 전달이 끝나도록 기다리는 함수.
void waitIdle(nookbit::BackgroundTaskRunner& runner) {
    if (!runner.busy()) return;
    QEventLoop loop;
    QObject::connect(&runner, &nookbit::BackgroundTaskRunner::busyChanged, &loop, [&] {
        if (!runner.busy()) loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    expect(!runner.busy(), "background work finishes within test timeout");
}
// 테스트 소유 경로에만 작은 입력 파일을 만드는 함수.
void createFile(const QString& path) {
    QFile file(path);
    expect(file.open(QIODevice::WriteOnly) && file.write("runtime fixture") == 15, "create input file");
}
// 사용자가 승인하기 전에는 원본을 유지하고 승인 후 이동·되돌리기가 연결되는지 검사하는 함수.
void testOrganizerWorkflow() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "organizer fixture"); return; }
    const auto appData = directory.filePath("appdata");
    expect(QDir().mkpath(appData), "create Organizer data directory");
    const auto home = directory.filePath("organized");
    expect(QDir().mkpath(home), "create organizing destination");
    const auto source = directory.filePath("photo.jpg");
    createFile(source);
    QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
    preferences.setValue("collisionPolicy", "skip");
    QPointer<nookbit::PendingFilesModel> queue;
    QPointer<nookbit::PreviewController> preview;
    QPointer<nookbit::RuleEditorController> rules;
    QPointer<nookbit::RecoveryController> recovery;
    QPointer<nookbit::HistoryController> history;
    QPointer<nookbit::BackgroundTaskRunner> tasks;
    nookbit::UiPageRegistry pages({2, 3, 8, 9, 11});
    {
        nookbit::FileOrganizerRuntime runtime({appData, home}, preferences);
        expect(runtime.registerUi(pages).hasValue() && pages.pages().size() == 5
            && pages.pageId("organizer.sort") == 7 && pages.pageId("organizer.rules") == 1
            && pages.pageId("organizer.history") == 4 && pages.pageId("organizer.recovery") == 10
            && pages.pageId("organizer.collision") == 6, "Organizer registers primary and auxiliary stable pages together");
        const auto sortInputs = pages.page(7).value("properties").toMap();
        expect(sortInputs.value("controller").value<QObject*>() == &runtime.preview()
            && sortInputs.value("pending").value<QObject*>() == &runtime.pendingFiles()
            && sortInputs.value("progress").value<QObject*>() == &runtime.tasks(), "Organizer supplies owned sorting inputs");
        expect(pages.page(1).value("properties").toMap().value("controller").value<QObject*>() == &runtime.rules()
            && pages.page(4).value("properties").toMap().value("controller").value<QObject*>() == &runtime.history(),
            "Organizer supplies owned rule and history inputs");
        expect(!runtime.registerUi(pages) && pages.pages().size() == 5, "duplicate feature registration leaves complete batch unchanged");
        expect(!pages.page(10).value("inMenu").toBool() && !pages.page(6).value("inMenu").toBool()
            && pages.page(7).value("inMenu").toBool(), "auxiliary pages are registered without adding home entries");
        const auto settings = runtime.settingsUi();
        expect(settings.value("source").toUrl().scheme() == "qrc"
            && settings.value("properties").toMap().value("sorting").value<QObject*>() == &runtime.preview()
            && settings.value("properties").toMap().value("destinations").value<QObject*>() == &runtime.destinations()
            && settings.value("notice").toMap().value("properties").toMap().value("controller").value<QObject*>() == &runtime.recovery(),
            "settings descriptor provides only feature-owned setting and recovery inputs");
        queue = &runtime.pendingFiles(); preview = &runtime.preview(); rules = &runtime.rules();
        recovery = &runtime.recovery(); history = &runtime.history(); tasks = &runtime.tasks();
        expect(QFile::exists(QDir(appData).filePath("rules.json")), "first startup seeds existing default rules");
        expect(preview->collisionPolicy() == "skip", "collision preference restored");
        const auto enqueued = runtime.pendingCommands().enqueuePaths({std::filesystem::path(source.toStdWString())});
        expect(enqueued && enqueued.value().added.size() == 1 && QFile::exists(source), "enqueue only registers the file");
    }
    expect(queue.isNull() && preview.isNull() && rules.isNull() && recovery.isNull() && history.isNull() && tasks.isNull(),
        "all Organizer UI endpoints die with owner");
    expect(pages.pages().isEmpty(), "Organizer destruction removes primary and auxiliary page entries");
    {
        nookbit::FileOrganizerRuntime runtime({appData, home}, preferences);
        expect(runtime.pendingFiles().count() == 1, "pending list restored after owner recreation");
        runtime.preview().createPreview(home);
        if (!runtime.preview().hasMovableSteps()) std::cerr << runtime.preview().message().toStdString() << '\n';
        expect(runtime.preview().hasMovableSteps(), "owned preview finds a matching rule");
        if (!runtime.preview().hasMovableSteps()) return;
        const auto destination = runtime.preview().steps().first().toMap().value("destination").toString();
        runtime.preview().prepareSort();
        waitIdle(runtime.tasks());
        expect(runtime.preview().awaitingConfirmation() && QFile::exists(source) && !QFile::exists(destination),
            "owned workflow prepares without moving before confirmation");
        runtime.preview().confirmSort();
        waitIdle(runtime.tasks());
        expect(!QFile::exists(source) && QFile::exists(destination) && runtime.pendingFiles().count() == 0,
            "confirmed workflow moves the file and persists queue removal");
        expect(runtime.preview().canUndo(), "completed move retains undo capability");
        runtime.preview().undoLastMove();
        waitIdle(runtime.tasks());
        expect(QFile::exists(source) && !QFile::exists(destination), "owned undo restores source");
        runtime.preview().setCollisionPolicy("abort");
    }
    expect(preferences.value("collisionPolicy").toString() == "abort", "owned setting connection persists changes");
    {
        nookbit::FileOrganizerRuntime runtime({appData, home}, preferences);
        expect(runtime.pendingFiles().count() == 0 && runtime.preview().collisionPolicy() == "abort",
            "completed queue state and changed preference survive restart");
    }
}
// 진행 중인 실제 준비 작업을 기다리며 승인 없이 이동하지 않는지 검사하는 함수.
void testOrganizerShutdown() {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "shutdown fixture"); return; }
    const auto appData = directory.filePath("appdata");
    expect(QDir().mkpath(appData), "create shutdown data directory");
    const auto source = directory.filePath("waiting.jpg");
    expect(QDir().mkpath(directory.filePath("organized")), "create shutdown destination");
    createFile(source);
    QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
    {
        nookbit::FileOrganizerRuntime runtime({appData, directory.filePath("organized")}, preferences);
        expect(runtime.pendingCommands().enqueuePaths({std::filesystem::path(source.toStdWString())}).hasValue(), "shutdown queue fixture");
        runtime.preview().createPreview(directory.filePath("organized"));
        if (!runtime.preview().hasMovableSteps()) std::cerr << runtime.preview().message().toStdString() << '\n';
        runtime.preview().prepareSort();
        expect(runtime.tasks().busy(), "real preparation is active at owner destruction");
    }
    QCoreApplication::processEvents();
    expect(QFile::exists(source), "destruction waits for preparation without approving or moving");
    // 완료 알림을 버려도 처리 스레드 자체는 끝까지 기다리는지 검사한다.
    std::atomic<bool> finished{};
    bool completed{};
    QSemaphore entered;
    {
        nookbit::FileOrganizerRuntime runtime({appData, directory.filePath("organized")}, preferences);
        waitIdle(runtime.tasks());
        expect(runtime.tasks().submit(&runtime.preview(), [&] {
            entered.release(); QThread::msleep(40); finished = true;
            return nookbit::Result<void>::success();
        }, [&](const nookbit::Result<void>&) { completed = true; }), "submit shutdown task");
        expect(entered.tryAcquire(1, 1000), "shutdown task started");
    }
    QCoreApplication::processEvents();
    expect(finished && !completed, "owner joins worker and drops queued completion after destruction");
}
// 팩 선택·개인 배율·좌표·프레임 편집을 복원하고 종료 뒤 콜백을 남기지 않는지 검사하는 함수.
void testCompanionOwnership(QGuiApplication& app) {
    QTemporaryDir directory;
    if (!directory.isValid()) { expect(false, "companion fixture"); return; }
    QSettings preferences(directory.filePath("preferences.ini"), QSettings::IniFormat);
    preferences.setValue("character/selectedPack", "builtin");
    preferences.setValue("character/scales/builtin", 0.75);
    preferences.setValue("character/autonomousWalk", false);
    preferences.setValue("character/windowShown", false);
    const QString resources = QStringLiteral(NOOKBIT_SOURCE_DIR "/resources");
    const QVariantMap edits{{"durations", QVariantMap{{"idle", QVariantMap{{"0", 800}}}}}};
    QPointer<nookbit::CharacterController> character;
    QPointer<nookbit::CharacterSettingsController> settings;
    QPointer<nookbit::CharacterWindowController> window;
    QPointer<nookbit::CompanionEditController> editor;
    QPointer<nookbit::CompanionInteractionController> interactions;
    QPointer<nookbit::CompanionMotionController> motion;
    QPointer<nookbit::BackgroundTaskRunner> imports;
    nookbit::UiPageRegistry pages({1, 7, 8});
    {
        nookbit::CompanionRuntime runtime({directory.path(), resources}, preferences, app);
        expect(runtime.startupResult().hasValue(), "companion restores bundled presentation and pack");
        expect(runtime.registerUi(pages).hasValue() && pages.pageId("companion.settings") == 5, "companion registers its own management page");
        expect(!runtime.registerUi(pages), "same companion page cannot be registered twice");
        const auto desktop = runtime.desktopUi();
        const auto desktopInputs = desktop.value("properties").toMap();
        expect(desktop.value("id").toString() == "companion.desktop"
            && desktop.value("source").toUrl().scheme() == "qrc"
            && desktopInputs.value("character").value<QObject*>() == &runtime.character()
            && desktopInputs.value("settings").value<QObject*>() == &runtime.settings()
            && desktopInputs.value("placement").value<QObject*>() == &runtime.window()
            && desktopInputs.value("motion").value<QObject*>() == &runtime.motion()
            && desktopInputs.value("interactions").value<QObject*>() == &runtime.interactions(),
            "desktop contribution supplies its source and explicit feature inputs");
        const auto inputs = pages.page(5).value("properties").toMap();
        expect(inputs.value("character").value<QObject*>() == &runtime.character()
            && inputs.value("settings").value<QObject*>() == &runtime.settings()
            && inputs.value("placement").value<QObject*>() == &runtime.window()
            && inputs.value("motion").value<QObject*>() == &runtime.motion()
            && inputs.value("edits").value<QObject*>() == &runtime.edits(), "page inputs come from the owning feature");
        character = &runtime.character(); settings = &runtime.settings(); window = &runtime.window();
        editor = &runtime.edits(); interactions = &runtime.interactions(); motion = &runtime.motion(); imports = &runtime.imports();
        expect(settings->selectedKey() == "builtin" && settings->userScale() == 0.75 && !window->shown() && !motion->enabled(),
            "existing pack, scale, visibility and walking preferences restored");
        settings->setUserScale(0.9);
        settings->setPlacement(QPointF(2, 3), QPointF(-4, -12));
        expect(editor->applyChanges(edits), "owned editor validates and stores personal frame durations");
        expect(settings->selectPack("builtin-nmoo") && settings->pivotOffset().isNull(), "other pack has independent placement");
        expect(settings->selectPack("builtin") && settings->userScale() == 0.9 && settings->pivotOffset() == QPointF(2, 3)
            && editor->applied() == edits, "switching pack restores scale, placement and edits together");
        // 저장 대기 타이머를 남기고 소유자를 해제하는 경우도 검사한다.
        window->moveAnchor(window->anchorX() - 10, window->anchorY());
    }
    expect(character.isNull() && settings.isNull() && window.isNull() && editor.isNull() && interactions.isNull() && motion.isNull() && imports.isNull(),
        "all Companion endpoints die with owner");
    expect(pages.pages().isEmpty(), "companion destruction removes page and menu metadata before borrowed endpoints expire");
    QCoreApplication::processEvents();
    const auto keys = preferences.allKeys();
    const auto savedScale = preferences.value("character/scales/builtin");
    {
        nookbit::CompanionRuntime runtime({directory.path(), resources, true}, preferences, app);
        expect(runtime.startupResult().hasValue() && runtime.settings().userScale() == 0.5 && runtime.window().shown()
            && runtime.edits().applied().isEmpty() && !runtime.motion().enabled(), "verification mode ignores personal settings");
        runtime.settings().setUserScale(1.2);
        runtime.settings().setPlacement(QPointF(5, 6), QPointF(7, 8));
        expect(runtime.edits().applyChanges(edits), "verification edits can be applied in memory");
    }
    expect(preferences.allKeys() == keys && preferences.value("character/scales/builtin") == savedScale,
        "verification mode does not persist personal settings");
    {
        nookbit::CompanionRuntime runtime({directory.path(), resources}, preferences, app);
        expect(runtime.startupResult().hasValue() && runtime.settings().userScale() == 0.9 && runtime.edits().applied() == edits,
            "companion recreation restores persisted scale and edits");
        // 새 실행기를 소유하는 동안 외부 완료 콜백은 아직 호출되지 않는다.
        std::atomic<bool> finished{};
        bool completed{};
        QSemaphore entered;
        {
            nookbit::CompanionRuntime temporary({directory.path(), resources, true}, preferences, app);
            expect(temporary.imports().submit(&temporary.settings(), [&] {
                entered.release(); QThread::msleep(40); finished = true;
                return nookbit::Result<void>::success();
            }, [&](const nookbit::Result<void>&) { completed = true; }), "submit independent import worker");
            expect(entered.tryAcquire(1, 1000), "import worker starts");
        }
        QCoreApplication::processEvents();
        expect(finished && !completed, "companion owner joins import worker and discards stale completion");
    }
    const auto snapshot = preferences.allKeys();
    // 종료된 타이머나 앱 신호 연결이 외부 설정을 다시 건드리지 않는지 검사한다.
    QEventLoop loop;
    QTimer::singleShot(230, &loop, &QEventLoop::quit);
    loop.exec();
    QMetaObject::invokeMethod(&app, "aboutToQuit", Qt::DirectConnection);
    expect(preferences.allKeys() == snapshot, "no stale application callback or delayed position save after destruction");
    {
        nookbit::CompanionRuntime broken({directory.path(), directory.path(), true}, preferences, app);
        expect(!broken.startupResult(), "missing required verification pack is a reported startup failure");
        expect(!broken.registerUi(pages) && pages.pages().isEmpty(), "failed companion startup contributes no page");
        expect(broken.desktopUi().isEmpty(), "failed companion startup contributes no desktop renderer");
    }
}
}
// 실제 화면 없이 기능 소유 묶음의 파일·설정·종료 계약을 검사하는 함수.
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    testOrganizerWorkflow();
    testOrganizerShutdown();
    testCompanionOwnership(app);
    if (!failures) std::cout << "All owned-runtime tests passed.\n";
    return failures ? 1 : 0;
}

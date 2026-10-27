#include "presentation/pending_files_model.hpp"
#include "presentation/file_icon_provider.hpp"
#include "presentation/file_preview_provider.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "infrastructure/filesystem/recovery_service.hpp"
#include "infrastructure/filesystem/file_operation_service.hpp"
#include "infrastructure/filesystem/windows_staged_copy.hpp"
#include "infrastructure/filesystem/windows_transfer_source_removal.hpp"
#include "infrastructure/filesystem/windows_transfer_file_inspector.hpp"
#include "infrastructure/filesystem/windows_transfer_recovery.hpp"
#include "infrastructure/filesystem/interrupted_operation_recovery_service.hpp"
#include "infrastructure/filesystem/safe_executor.hpp"
#include "infrastructure/configuration/runtime_settings_repository.hpp"
#include <nookbit/application/resume_interrupted_transfer_use_case.hpp>
#include "infrastructure/persistence_sqlite/sqlite_transfer_journal.hpp"
#include "application/use_cases/evaluate_watch_folder_use_case.hpp"
#include "application/use_cases/create_preview_use_case.hpp"
#include "application/use_cases/freeze_preview_use_case.hpp"
#include "application/use_cases/verify_saved_plan_use_case.hpp"
#include "application/use_cases/approve_plan_use_case.hpp"
#include "application/use_cases/test_rule_use_case.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/configuration/json_watch_settings_repository.hpp"
#include "infrastructure/configuration/json_pending_files_repository.hpp"
#include "infrastructure/filesystem/filesystem_scanner.hpp"
#include "modules/rules/rule_engine.hpp"
#include "modules/planner/planner.hpp"
#include "infrastructure/cryptography/qt_plan_hasher.hpp"
#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontInfo>
#include <QApplication>
#include <QQuickStyle>
#include <QPalette>
#include "presentation/ui_settings_controller.hpp"
#include "presentation/destination_settings_controller.hpp"
#include "presentation/user_guide_controller.hpp"
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>
#include "presentation/app_lifecycle_controller.hpp"
#include <QMetaObject>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlProperty>
#include <memory>
#include <QEventLoop>
#include <QQmlContext>
#include <QStandardPaths>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include "presentation/companion_edit_controller.hpp"
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include "app/thread_local_operations.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/notification_controller.hpp"
#include "presentation/companion_interaction_controller.hpp"
#include "presentation/companion_motion_controller.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/character_window_controller.hpp"
#include <QScreen>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QLineF>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QJSValue>
#include "infrastructure/configuration/json_character_presentation.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/directory_image_pack_library.hpp"

// 내장 SQL로 임시 메모리 DB를 최신 스키마까지 만들 수 있는지 검사하는 함수.
static bool verifyBundledHistorySchema(const QString& migrationPath) {
    const auto connectionName = QStringLiteral("nookbit-bundled-schema-check");
    bool ready = false;
    {
        auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        database.setDatabaseName(QStringLiteral(":memory:"));
        if (database.open()) {
            const auto migrated = nookbit::migrateHistorySchema(database, migrationPath);
            if (migrated) {
                QSqlQuery proofTable(database);
                ready = proofTable.exec(QStringLiteral("SELECT count(*) FROM transfer_removal_proofs")) &&
                        proofTable.next() && proofTable.value(0).toInt() == 0;
            } else {
                QTextStream(stderr) << QString::fromStdString(migrated.error().message) << Qt::endl;
            }
        }
        database.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return ready;
}

// 실행 파일에 내장된 여러 해상도의 공통 앱 아이콘을 읽는 함수.
static QIcon applicationIcon() {
    return QIcon(QStringLiteral(":/nookbit/icons/nookbit.ico"));
}

// 공통 버튼의 실제 호버·누름·선택·짧은 클릭·비활성을 밝고 어두운 테마에서 검사하는 함수.
static bool verifyButtonFeedback(QQmlEngine& engine, QObject* metrics) {
    QQuickWindow host;
    host.resize(300, 110);
    // 상태 애니메이션이 화면 프레임에 반영되기를 기다리는 함수.
    const auto settle = [](int ms) {
        QEventLoop events;
        QTimer::singleShot(ms, &events, &QEventLoop::quit);
        events.exec();
    };
    // 사용자 화면 대신 별도 검사 창에 포인터 입력을 보내는 함수.
    const auto pointer = [&host](QEvent::Type type, QPointF position, Qt::MouseButton button, Qt::MouseButtons buttons) {
        QMouseEvent event(type, position, QPointF(host.mapToGlobal(position.toPoint())), button, buttons, Qt::NoModifier);
        QCoreApplication::sendEvent(&host, &event);
    };
    for (const auto& type : {QStringLiteral("AppButton"), QStringLiteral("LargeActionButton"), QStringLiteral("HomeMenuButton")}) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nimport NookBit\n%1 { text: 'Feedback'; x: 40; y: 20; width: 220; height: 64; checkable: true }")
            .arg(type).toUtf8(), QUrl(QStringLiteral("qrc:/button-feedback-fixture.qml")));
        std::unique_ptr<QObject> object(component.create());
        auto* button = qobject_cast<QQuickItem*>(object.get());
        if (!button) { QTextStream(stderr) << component.errorString(); return false; }
        button->setParentItem(host.contentItem());
        if (type == "LargeActionButton") button->setProperty("primary", true);
        auto* surface = button->findChild<QQuickItem*>(QStringLiteral("buttonFeedbackSurface"));
        if (!surface) return false;
        host.show();
        for (const bool dark : {false, true}) {
            // 실패한 버튼 종류와 상태를 표시하는 함수.
            const auto fail = [&](const char* state) {
                QTextStream(stderr) << "Button feedback failed: " << type << " dark=" << dark << " state=" << state << Qt::endl;
                return false;
            };
            QQmlProperty(button, QStringLiteral("dark")).write(dark);
            if (button->property("dark").toBool() != dark) return fail("theme");
            button->setProperty("enabled", true);
            button->setProperty("checked", false);
            pointer(QEvent::MouseMove, QPointF(5, 5), Qt::NoButton, Qt::NoButton);
            settle(150);
            const auto normal = surface->property("color").value<QColor>();
            pointer(QEvent::MouseMove, QPointF(100, 45), Qt::NoButton, Qt::NoButton);
            settle(150);
            const auto hover = surface->property("color").value<QColor>();
            if (!button->property("hovered").toBool() || normal == hover) return fail("hover");
            button->setProperty("down", true);
            settle(20);
            if (surface->property("color").value<QColor>() == hover || surface->scale() >= 1 || button->width() != 220 || button->height() != 64) return fail("press");
            button->setProperty("down", false);
            button->setProperty("checked", true);
            settle(150);
            auto* text = type == "HomeMenuButton" ? button->findChild<QObject*>(QStringLiteral("homeMenuLabel"))
                : button->property("contentItem").value<QQuickItem*>();
            if (!text || text->property("color").value<QColor>() != button->property("buttonForeground").value<QColor>()) {
                QTextStream(stderr) << "Button text=" << (text ? text->property("color").value<QColor>().name() : "missing")
                    << " expected=" << button->property("buttonForeground").value<QColor>().name()
                    << " dark=" << button->property("dark").toBool() << Qt::endl;
                return fail("selected text");
            }
            pointer(QEvent::MouseButtonPress, QPointF(100, 45), Qt::LeftButton, Qt::LeftButton);
            pointer(QEvent::MouseButtonRelease, QPointF(100, 45), Qt::LeftButton, Qt::NoButton);
            if (!surface->property("recentClick").toBool()) return fail("click acknowledgement");
            button->setProperty("enabled", false);
            settle(metrics->property("buttonClickPulseMs").toInt() + metrics->property("buttonReleaseMs").toInt() + 30);
            if (surface->property("pushed").toBool() || surface->opacity() >= 1) return fail("disabled");
        }
        host.hide();
    }
    return true;
}

// 화면 밖 자동 검사에서 클릭·끌기·쓰다듬기가 서로 중복 실행되지 않는지 검사하는 함수.
static bool verifyCompanionGestures(QQuickWindow* window, nookbit::CharacterWindowController& placement,
    nookbit::CompanionInteractionController& interactions, nookbit::CharacterController& character) {
    if (!window) return false;
    const QPoint original(placement.anchorX(), placement.anchorY());
    const bool originallyShown = placement.shown();
    placement.setShown(true);
    QCoreApplication::processEvents();
    auto* area = window->findChild<QQuickItem*>(QStringLiteral("companionGestureInput"));
    if (!area) return false;
    const QPointF point(window->width() / 2.0, window->height() / 2.0);
    // 실제 포인터 경로를 따라 QML 입력 판정 코드까지 실행하는 함수.
    const auto send = [window](QEvent::Type type, QPointF position, Qt::MouseButton button,
        Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QMouseEvent event(type, position, window->mapToGlobal(position.toPoint()), button, buttons, modifiers);
        QCoreApplication::sendEvent(window, &event);
        QCoreApplication::processEvents();
    };
    send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    bool ready = character.reactionId().isEmpty();
    send(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton);
    ready = ready && !character.reactionId().isEmpty() && !interactions.active();
    interactions.cancel();
    send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton);
    send(QEvent::MouseMove, point + QPointF(60, 0), Qt::NoButton, Qt::LeftButton);
    ready = ready && interactions.active() && area->property("gestureKind").toString() == "companion.drag" &&
        QPoint(placement.anchorX(), placement.anchorY()) != original;
    send(QEvent::MouseButtonRelease, point + QPointF(60, 0), Qt::LeftButton, Qt::NoButton);
    ready = ready && !interactions.active() && character.message() == QStringLiteral("여기로 왔어요.");
    interactions.cancel();
    placement.moveAnchor(original.x(), original.y());
    send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    send(QEvent::MouseMove, point + QPointF(20, 0), Qt::NoButton, Qt::LeftButton, Qt::ShiftModifier);
    ready = ready && interactions.active() && area->property("gestureKind").toString() == "companion.pet" &&
        QPoint(placement.anchorX(), placement.anchorY()) == original;
    send(QEvent::MouseButtonRelease, point + QPointF(20, 0), Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    ready = ready && !interactions.active() && !character.reactionId().isEmpty();
    interactions.cancel();
    send(QEvent::MouseButtonPress, point, Qt::LeftButton, Qt::LeftButton, Qt::ShiftModifier);
    ready = ready && interactions.active() && QMetaObject::invokeMethod(area, "canceled");
    ready = ready && !interactions.active() && character.reactionId().isEmpty();
    send(QEvent::MouseButtonRelease, point, Qt::LeftButton, Qt::NoButton, Qt::ShiftModifier);
    interactions.cancel();
    placement.moveAnchor(original.x(), original.y());
    placement.setShown(originallyShown);
    if (!ready) QTextStream(stderr) << "Companion gesture routing failed" << Qt::endl;
    return ready;
}

// 실제 확장 창에 마우스 이벤트를 보내 모든 페이지의 접기 버튼 접근성을 검사하는 함수.
static bool verifyExtensionInteraction(QQuickWindow* mainWindow, QQuickWindow* extension, QObject* metrics,
                                       nookbit::NotificationController& notifications) {
    auto* button = mainWindow->findChild<QQuickItem*>(QStringLiteral("extensionCloseButton"));
    auto* rules = mainWindow->findChild<QQuickItem*>(QStringLiteral("ruleEditorPage"));
    if (!button || !rules) return false;
    // 창 안의 실제 버튼 좌표로 눌렀다 떼는 입력을 전달하는 함수.
    const auto click = [extension](QQuickItem* item) {
        // 크기 복원 뒤 렌더링 프레임의 레이아웃·클리핑 갱신을 기다린다.
        QEventLoop layoutFrame;
        QTimer::singleShot(40, &layoutFrame, &QEventLoop::quit);
        layoutFrame.exec();
        const auto local = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        const QPointF global = extension->mapToGlobal(local.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(extension, &press);
        QCoreApplication::sendEvent(extension, &release);
        QCoreApplication::processEvents();
    };
    for (const int page : {1, 3, 4, 5, 6, 7, 8, 9, 10, 11}) {
        mainWindow->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        auto* home = mainWindow->findChild<QObject*>(QStringLiteral("compactHome"));
        const int expectedMenu = (page == 3 || page == 6 || page == 8 || page == 9 || page == 10) ? 8 : page;
        if (!home || home->property("selectedPage").toInt() != expectedMenu) return false;
        if (page == 1) {
            auto* ruleTestButton = rules->findChild<QQuickItem*>(QStringLiteral("ruleTestButton"));
            auto* testPanel = rules->findChild<QObject*>(QStringLiteral("ruleTestPanel"));
            if (!rules->isVisible() || rules->width() <= 0 || rules->height() <= 0 || !ruleTestButton || !testPanel) {
                QTextStream(stderr) << "Rule editor is hidden or covered in the extension window." << Qt::endl;
                return false;
            }
            click(ruleTestButton);
            if (!testPanel->property("opened").toBool()) {
                QTextStream(stderr) << "Rule editor toolbar click is blocked." << Qt::endl;
                return false;
            }
            // 다른 페이지로 이동하면 테스트 창을 닫고 돌아와도 자동으로 다시 열지 않는지 검사한다.
            const auto graphBeforeLeave = rules->property("blocks").value<QJSValue>().toVariant();
            mainWindow->setProperty("currentPage", 8);
            QCoreApplication::processEvents();
            if (testPanel->property("visible").toBool()) return false;
            mainWindow->setProperty("currentPage", 1);
            QCoreApplication::processEvents();
            if (testPanel->property("visible").toBool() || rules->property("blocks").value<QJSValue>().toVariant() != graphBeforeLeave) return false;
            click(ruleTestButton);
            if (!testPanel->property("opened").toBool()) return false;
            QMetaObject::invokeMethod(testPanel, "close");
            // 규칙 문서 버튼이 읽기 창 대신 직접 저장 창구에 연결되는지 검사한다.
            auto* guideButton = rules->findChild<QQuickItem*>(QStringLiteral("rulesUserGuideButton"));
            auto* guide = rules->findChild<QObject*>(QStringLiteral("rulesGuideDownload"));
            if (!guideButton || !guide || guide->property("guideKey").toString() != "rules" ||
                !guide->findChild<QObject*>(QStringLiteral("guideSaveDialog"))) return false;
            // 추가 메뉴가 조건·분기·종착점을 중복 없이 나누고 제한된 크기로 열리는지 검사한다.
            const auto groups = rules->property("nodeAddGroups").value<QJSValue>().toVariant().toList();
            if (groups.size() != 3) return false;
            const QStringList expectedTypes{"extension", "size", "name", "age", "otherwise", "destination", "exclude"};
            QStringList actualTypes;
            for (const auto& group : groups) {
                for (const auto& node : group.toMap().value("nodes").toList())
                    actualTypes.append(node.toMap().value("type").toString());
            }
            if (actualTypes != expectedTypes || !QMetaObject::invokeMethod(rules, "openAddMenu",
                    Q_ARG(QVariant, QVariant(100)), Q_ARG(QVariant, QVariant(100)))) return false;
            QCoreApplication::processEvents();
            auto* addMenu = rules->findChild<QObject*>(QStringLiteral("addNodeMenu"));
            if (!addMenu || !addMenu->property("opened").toBool() || addMenu->property("height").toDouble() <= 0 ||
                addMenu->property("width").toInt() != metrics->property("nodeAddMenuWidth").toInt()) return false;
            mainWindow->setProperty("currentPage", 8);
            QCoreApplication::processEvents();
            if (addMenu->property("visible").toBool()) return false;
            mainWindow->setProperty("currentPage", 1);
            QCoreApplication::processEvents();
            // 새 크기 블록의 선택 목록이 초안을 직접 갱신하는지 검사한다.
            if (!QMetaObject::invokeMethod(rules, "addBlock", Q_ARG(QVariant, QVariant("size")),
                    Q_ARG(QVariant, QVariant(350)), Q_ARG(QVariant, QVariant(220)))) return false;
            QCoreApplication::processEvents();
            auto* comparison = rules->findChild<QObject*>(QStringLiteral("sizeComparisonCombo"));
            auto* unit = rules->findChild<QObject*>(QStringLiteral("sizeUnitCombo"));
            if (!comparison || !unit || unit->property("count").toInt() != 3) return false;
            // 공통 드롭다운의 높이·화살표 규격과 실제 메뉴 열림을 검사한다.
            for (auto* combo : {comparison, unit}) {
                auto* indicator = combo->property("indicator").value<QObject*>();
                if (combo->property("height").toInt() != metrics->property("controlHeight").toInt() || !indicator
                    || indicator->objectName() != QStringLiteral("appDropdownIndicator")
                    || indicator->property("width").toInt() != metrics->property("dropdownIconWidth").toInt()
                    || indicator->property("height").toInt() != metrics->property("dropdownIconHeight").toInt()) {
                    QTextStream(stderr) << "Dropdown dimensions failed: " << combo->objectName()
                        << " height=" << combo->property("height").toInt() << Qt::endl;
                    return false;
                }
                auto* popup = combo->property("popup").value<QObject*>();
                click(qobject_cast<QQuickItem*>(combo));
                if (!popup || !popup->property("visible").toBool()) {
                    auto* item = qobject_cast<QQuickItem*>(combo);
                    QTextStream(stderr) << "Dropdown popup did not open: " << combo->objectName()
                        << " at " << item->mapToScene(QPointF()).x() << "," << item->mapToScene(QPointF()).y()
                        << " window=" << extension->width() << "x" << extension->height()
                        << " item=" << item->width() << "x" << item->height()
                        << " visible=" << item->isVisible() << Qt::endl;
                    return false;
                }
                QMetaObject::invokeMethod(popup, "close");
                QCoreApplication::processEvents();
            }
            comparison->setProperty("currentIndex", 1);
            QMetaObject::invokeMethod(comparison, "activated", Q_ARG(int, 1));
            unit->setProperty("currentIndex", 2);
            QMetaObject::invokeMethod(unit, "activated", Q_ARG(int, 2));
            const auto selected = rules->property("selectedBlock").value<QJSValue>();
            if (selected.property("comparison").toString() != "atLeast" || selected.property("unit").toString() != "GB") return false;
        }
        if (page == 4) {
            auto* list = mainWindow->findChild<QQuickItem*>(QStringLiteral("historyList"));
            if (!list) return false;
            const auto originalModel = list->property("model");
            QVariantList entries;
            for (int row = 0; row < 3; ++row) {
                entries.append(QVariantMap{{"planId", QString::number(row)}, {"time", "12:00"},
                    {"status", QStringLiteral("이동 완료")}, {"fileCount", 1}, {"appliedCount", 1},
                    {"dateGroup", row < 2 ? QStringLiteral("오늘") : QStringLiteral("어제")},
                    {"startsDateGroup", row != 1}});
            }
            list->setProperty("model", entries);
            QEventLoop historyFrame;
            QTimer::singleShot(40, &historyFrame, &QEventLoop::quit);
            historyFrame.exec();
            QStringList headings;
            // 화면 트리에서 같은 날짜의 중복 제목 없이 실제 표시된 제목을 찾는 함수.
            const auto collectHeadings = [&headings](auto&& self, QQuickItem* item) -> void {
                if (item->objectName() == QStringLiteral("historyDateHeading") && item->isVisible())
                    headings.append(item->property("text").toString());
                for (auto* child : item->childItems()) self(self, child);
            };
            collectHeadings(collectHeadings, list);
            list->setProperty("model", originalModel);
            if (headings != QStringList{QStringLiteral("오늘"), QStringLiteral("어제")}) return false;
        }
        if (page == 8) {
            auto* themeSwitch = mainWindow->findChild<QQuickItem*>(QStringLiteral("globalDarkModeSwitch"));
            const bool initial = mainWindow->property("editorDark").toBool();
            if (!themeSwitch) return false;
            click(themeSwitch);
            if (mainWindow->property("editorDark").toBool() == initial || rules->property("darkMode").toBool() == initial) return false;
            // 네이티브 메뉴와 QML 창이 같은 테마 색상표를 사용하는지 검사한다.
            const auto themeColors = mainWindow->property("theme").value<QJSValue>().toVariant().toMap();
            if (QGuiApplication::palette().color(QPalette::Window) != QColor(themeColors.value("window").toString()) ||
                QGuiApplication::palette().color(QPalette::Text) != QColor(themeColors.value("text").toString())) return false;
            click(themeSwitch);
            if (mainWindow->property("editorDark").toBool() != initial) return false;
        }
        if (page == 3) {
            // 감시 조건 네 항목에 실제 키 입력 후 Enter로 숫자를 확정하는 검사.
            for (const auto* name : {"watchIntervalSpin", "watchCountSpin", "watchSizeSpin", "watchAgeSpin"}) {
                auto* spin = mainWindow->findChild<QQuickItem*>(QString::fromLatin1(name));
                if (!spin || !spin->property("editable").toBool()
                    || spin->height() != metrics->property("controlHeight").toInt()) return false;
                const bool wasEnabled = spin->isEnabled();
                spin->setEnabled(true);
                auto* editor = qobject_cast<QQuickItem*>(spin->property("contentItem").value<QObject*>());
                if (!editor || editor->property("readOnly").toBool()) return false;
                editor->forceActiveFocus();
                extension->requestActivate();
                QCoreApplication::processEvents();
                QKeyEvent selectAll(QEvent::KeyPress, Qt::Key_A, Qt::ControlModifier, QStringLiteral("a"));
                QKeyEvent three(QEvent::KeyPress, Qt::Key_3, Qt::NoModifier, QStringLiteral("3"));
                QKeyEvent seven(QEvent::KeyPress, Qt::Key_7, Qt::NoModifier, QStringLiteral("7"));
                QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
                for (auto* event : {&selectAll, &three, &seven, &enter}) QCoreApplication::sendEvent(extension, event);
                QKeyEvent enterRelease(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
                QCoreApplication::sendEvent(extension, &enterRelease);
                QCoreApplication::processEvents();
                if (spin->property("value").toInt() != 37) {
                    QTextStream(stderr) << "Keyboard number entry failed: " << name << " text=" << editor->property("text").toString()
                        << " value=" << spin->property("value").toInt() << Qt::endl;
                    return false;
                }
                // 둥근 증감 버튼을 실제로 눌러 키보드 입력과 같은 값을 조절하는지 검사한다.
                auto* increase = spin->findChild<QQuickItem*>(QStringLiteral("spinIncreaseIndicator"));
                auto* decrease = spin->findChild<QQuickItem*>(QStringLiteral("spinDecreaseIndicator"));
                if (!increase || !decrease || increase->width() != metrics->property("numericStepSize").toInt()) return false;
                click(increase);
                if (spin->property("value").toInt() != 38) return false;
                click(decrease);
                if (spin->property("value").toInt() != 37) return false;
                spin->setEnabled(wasEnabled);
            }
            auto* sizeUnit = mainWindow->findChild<QObject*>(QStringLiteral("watchSizeUnitCombo"));
            if (!sizeUnit || sizeUnit->property("count").toInt() != 4) return false;
        }
        if (page == 8) {
            // 설정 제목과 구분선이 세 그룹에 같은 규격으로 배치되는지 검사한다.
            auto* settings = mainWindow->findChild<QObject*>(QStringLiteral("settingsPage"));
            if (!settings) return false;
            const auto titles = settings->findChildren<QQuickItem*>(QStringLiteral("settingsSectionTitle"));
            const auto dividers = settings->findChildren<QQuickItem*>(QStringLiteral("settingsSectionDivider"));
            if (titles.size() != 3 || dividers.size() != 3) return false;
            for (auto* title : titles) {
                if (title->property("font").value<QFont>().pixelSize() != metrics->property("textSectionTitle").toInt() ||
                    !title->isVisible() || title->height() <= 0) return false;
            }
            for (auto* divider : dividers) {
                if (!divider->isVisible() || divider->height() != metrics->property("borderNormal").toInt() || divider->width() <= 0) return false;
            }
        }
        if (page == 11) {
            // Repeater 안의 알림 스위치를 실제 화면 트리에서 찾는 함수.
            const auto findToggle = [](auto&& self, QQuickItem* item) -> QQuickItem* {
                if (item->objectName() == QStringLiteral("notificationToggle_file.offer")) return item;
                for (auto* child : item->childItems()) if (auto* found = self(self, child)) return found;
                return nullptr;
            };
            auto* toggle = findToggle(findToggle, extension->contentItem());
            if (!toggle || !notifications.isEnabled("file.offer") ||
                !notifications.publish({"file.offer", "verify-offer", true, "Test", "Test notice"})) return false;
            click(toggle);
            if (notifications.isEnabled("file.offer") || notifications.activeTypes().contains("file.offer")) return false;
            toggle = findToggle(findToggle, extension->contentItem());
            if (!toggle) return false;
            click(toggle);
            if (!notifications.isEnabled("file.offer") || !notifications.activeTypes().contains("file.offer")) return false;
            if (!notifications.publish({"file.offer", "verify-offer", false, {}, {}})) return false;
        }
        if (page == 1) {
            auto* testPanel = rules->findChild<QObject*>(QStringLiteral("ruleTestPanel"));
            if (!testPanel || !QMetaObject::invokeMethod(testPanel, "open")) return false;
        }
        click(button);
        if (mainWindow->property("currentPage").toInt() != 0 || extension->isVisible()) {
            QTextStream(stderr) << "Extension close click blocked on page " << page << Qt::endl;
            return false;
        }
        if (page == 1 && rules->findChild<QObject*>(QStringLiteral("ruleTestPanel"))->property("visible").toBool()) return false;
    }
    return true;
}

// 파일 정리의 임시 미리보기가 창·입력 상태를 보존하며 돌아가기와 Esc로 닫히는지 검사하는 함수.
static bool verifyInlineFilePreview(QQuickWindow* window, QQuickItem* page) {
    auto* button = page->findChild<QObject*>(QStringLiteral("compactPreviewButton"));
    auto* preview = page->findChild<QQuickItem*>(QStringLiteral("fileSortPreview"));
    auto* back = page->findChild<QQuickItem*>(QStringLiteral("filePreviewBackButton"));
    auto* destination = page->findChild<QObject*>(QStringLiteral("compactDestinationField"));
    if (!button || !preview || !back || !destination) return false;
    const auto size = window->size();
    const auto position = window->position();
    const auto windows = QGuiApplication::allWindows().size();
    const auto text = destination->property("text").toString();
    QMetaObject::invokeMethod(button, "clicked");
    QEventLoop layout;
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    auto* input = page->findChild<QQuickItem*>(QStringLiteral("fileSortScroll"));
    if (!preview->isVisible() || preview->window() != window || window->size() != size
        || window->position() != position || QGuiApplication::allWindows().size() != windows
        || window->transientParent()->property("currentPage").toInt() != 7
        || !input || input->isVisible() || std::abs(preview->x()) > 1
        || page->property("transitioning").toBool()) return false;
    const auto point = back->mapToScene(QPointF(back->width() / 2, back->height() / 2));
    const QPointF global = window->mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QCoreApplication::sendEvent(window, &release);
    if (page->property("previewOpen").toBool() || destination->property("text").toString() != text) return false;
    QMetaObject::invokeMethod(button, "clicked");
    window->requestActivate();
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QKeyEvent escapeRelease(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &escape);
    QCoreApplication::sendEvent(window, &escapeRelease);
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    auto* controller = preview->property("controller").value<QObject*>();
    if (!controller) return false;
    // 백그라운드 알림만으로는 화면이 바뀌지 않고 수동 요청에서만 단계가 전환되는지 검사한다.
    QMetaObject::invokeMethod(controller, "sortExecutionStarted");
    if (page->property("workflowStep").toInt() != 0) return false;
    page->setProperty("manualSortRequested", true);
    QMetaObject::invokeMethod(controller, "sortExecutionStarted");
    if (page->property("workflowStep").toInt() != 3) return false;
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    QQuickItem* progress = nullptr;
    QQuickItem* complete = nullptr;
    for (auto* item : page->childItems()) {
        if (item->objectName() == QStringLiteral("fileSortProgress")) progress = item;
        if (item->objectName() == QStringLiteral("fileSortComplete")) complete = item;
    }
    if (!progress || !complete || !progress->isVisible() || input->isVisible() || preview->isVisible()) return false;
    QMetaObject::invokeMethod(controller, "sortExecutionFinished", Q_ARG(bool, false), Q_ARG(QString, QString("test failure")));
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    if (page->property("workflowStep").toInt() != 4 || !complete->isVisible()
        || complete->property("success").toBool() || progress->isVisible()) return false;
    auto* doneBack = complete->findChild<QObject*>(QStringLiteral("fileCompleteBackButton"));
    if (!doneBack || doneBack->property("text").toString().length() != 0) return false;
    QMetaObject::invokeMethod(doneBack, "clicked");
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    if (controller) QMetaObject::invokeMethod(controller, "clear");
    QTimer::singleShot(40, &layout, &QEventLoop::quit);
    layout.exec();
    // 확인 화면 자체는 이동 권한을 만들지 않고 취소하면 직전 미리보기로 복귀하는지 검사한다.
    page->setProperty("confirmationReturnStep", 1);
    page->setProperty("manualSortRequested", true);
    QMetaObject::invokeMethod(page, "showStep", Q_ARG(QVariant, QVariant(2)));
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    auto* confirmation = page->findChild<QQuickItem*>(QStringLiteral("fileSortConfirmation"));
    auto* confirmButton = page->findChild<QQuickItem*>(QStringLiteral("fileConfirmationExecuteButton"));
    auto* cancelButton = page->findChild<QQuickItem*>(QStringLiteral("fileConfirmationCancelButton"));
    auto* previewButton = page->findChild<QQuickItem*>(QStringLiteral("compactPreviewButton"));
    auto* confirmBack = page->findChild<QObject*>(QStringLiteral("fileConfirmationBackButton"));
    auto* oldDialog = window->transientParent()->findChild<QObject*>(QStringLiteral("legacySortConfirmation"));
    if (!confirmation || !confirmation->isVisible() || confirmation->window() != window
        || !confirmButton || !cancelButton || !previewButton || confirmButton->isEnabled() || !confirmBack || !oldDialog
        || confirmButton->height() != cancelButton->height() || confirmButton->height() != previewButton->height()
        || std::abs(confirmButton->width() - cancelButton->width()) > 1
        || oldDialog->property("visible").toBool() || window->size() != size) return false;
    QMetaObject::invokeMethod(confirmBack, "clicked");
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    if (page->property("workflowStep").toInt() != 1 || !preview->isVisible()) return false;
    QCoreApplication::sendEvent(window, &escape);
    QCoreApplication::sendEvent(window, &escapeRelease);
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    return !page->property("previewOpen").toBool() && window->size() == size
        && input->isVisible() && !preview->isVisible() && !page->property("transitioning").toBool()
        && destination->property("text").toString() == text;
}

// 아이콘과 글씨 길이에 관계없이 메뉴 글씨 중심이 버튼 전체 중심과 일치하는지 검사하는 함수.
static bool verifyHomeMenuAlignment(QQuickWindow* root, QObject* metrics) {
    auto* home = root->findChild<QQuickItem*>(QStringLiteral("compactHome"));
    if (!home) return false;
    // Repeater의 대리 항목을 포함한 실제 화면 트리에서 이름으로 찾는 함수.
    const auto find = [](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems()) if (auto* found = self(self, child, name)) return found;
        return nullptr;
    };
    for (const int page : {7, 1, 4, 5, 8, 11}) {
        auto* button = find(find, home, QStringLiteral("homeNavigation%1").arg(page));
        auto* label = button ? find(find, button, QStringLiteral("homeMenuLabel")) : nullptr;
        auto* icon = button ? find(find, button, QStringLiteral("homeMenuIcon")) : nullptr;
        if (!button || !label || !icon) return false;
        const auto center = label->mapToItem(button, QPointF(label->width() / 2, label->height() / 2));
        const auto iconOrigin = icon->mapToItem(button, QPointF());
        if (std::abs(center.x() - button->width() / 2) > 1
            || std::abs(center.y() - button->height() / 2) > 1
            || std::abs(iconOrigin.x() - metrics->property("controlPadding").toInt()) > 1
            || icon->width() != metrics->property("mainMenuIconSize").toInt()
            || button->height() != metrics->property("mainNavigationHeight").toInt()) return false;
    }
    return true;
}

// 무파일 작업으로 검사·되돌리기 표시가 서브창에 속하고 제목 이동은 잠기지 않는지 검사하는 함수.
static bool verifyBusyPresentation(QQuickWindow* root, QQuickWindow* extension, nookbit::BackgroundTaskRunner& runner) {
    auto* popup = root->findChild<QObject*>(QStringLiteral("operationProgressPopup"));
    auto* veil = root->findChild<QQuickItem*>(QStringLiteral("operationProgressVeil"));
    auto* recoveryIndicator = root->findChild<QQuickItem*>(QStringLiteral("recoveryInspectionIndicator"));
    auto* drag = root->findChild<QQuickItem*>(QStringLiteral("homeWindowDragArea"));
    auto* menu = root->findChild<QQuickItem*>(QStringLiteral("homeNavigationMenu"));
    if (!popup || !veil || !recoveryIndicator || !drag || !menu) return false;
    using Kind = nookbit::BackgroundTaskRunner::TaskKind;
    struct BusyCase { int page; Kind kind; };
    const BusyCase cases[] = {{7, Kind::Inspection}, {4, Kind::FileWork},
        {10, Kind::Inspection}, {10, Kind::FileWork}, {0, Kind::FileWork}};
    for (const auto& test : cases) {
        const int page = test.page;
        const bool inlineInspection = page == 10 && test.kind == Kind::Inspection;
        const bool showPopup = page != 0 && !inlineInspection;
        root->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        const auto gate = std::make_shared<QSemaphore>();
        const auto finished = std::make_shared<bool>(false);
        // 화면 상태 검사를 기다리기만 하고 파일에 접근하지 않는 함수.
        if (!runner.submit(root, [gate] { gate->acquire(); return nookbit::Result<void>::success(); },
                [finished](const auto&) { *finished = true; }, test.kind)) return false;
        QEventLoop frame;
        QTimer::singleShot(40, &frame, &QEventLoop::quit);
        frame.exec();
        auto* content = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>());
        const bool valid = drag->isEnabled() && !menu->isEnabled() && root->contentItem()->isEnabled()
            && !popup->property("modal").toBool()
            && popup->property("visible").toBool() == showPopup
            && veil->isVisible() == showPopup
            && recoveryIndicator->isVisible() == inlineInspection
            && (!inlineInspection || recoveryIndicator->window() == extension)
            && (!showPopup || (content && content->window() == extension && veil->window() == extension
                && veil->property("color").value<QColor>() == QColor(Qt::white)
                && std::abs(popup->property("x").toDouble() + popup->property("width").toDouble() / 2 - extension->width() / 2.0) < 1
                && std::abs(popup->property("y").toDouble() + popup->property("height").toDouble() / 2
                    - (veil->mapToScene(QPointF()).y() + veil->height() / 2)) < 1));
        gate->release();
        QTimer::singleShot(150, &frame, &QEventLoop::quit);
        frame.exec();
        if (!valid || !*finished || runner.busy() || veil->isVisible() || recoveryIndicator->isVisible()) return false;
    }
    return true;
}

// 상단 안내 밖의 드롭·중복·폴더 거절·아이콘 로딩이 원본을 바꾸지 않는지 검사하는 함수.
static bool verifyPageFileDrop(QQuickWindow* window, QQuickItem* page, nookbit::PendingFilesModel& pending) {
    QTemporaryDir directory;
    if (!directory.isValid() || !page) return false;
    const auto path = directory.filePath(QStringLiteral("report.pdf"));
    QFile fixture(path);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write("unchanged") != 9) return false;
    fixture.close();
    auto* area = page->findChild<QQuickItem*>(QStringLiteral("fileSortPageDropArea"));
    if (!area || area->width() != page->width() || area->height() != page->height()) return false;
    // 상단 안내 밖인 페이지 아래쪽에 외부 파일 드래그 이벤트를 전달하는 함수.
    const auto drop = [window, page](const QUrl& url, Qt::DropActions actions) {
        QMimeData mime;
        mime.setUrls({url});
        const auto point = page->mapToScene(QPointF(page->width() / 2, page->height() - 30)).toPoint();
        QDragEnterEvent enter(point, actions, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &enter);
        auto* overlay = page->findChild<QQuickItem*>(QStringLiteral("fileDropOverlay"));
        if ((actions & Qt::CopyAction) && (!overlay || !overlay->isVisible())) return false;
        QDropEvent dropped(point, actions, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &dropped);
        QCoreApplication::processEvents();
        return dropped.isAccepted() && dropped.dropAction() == Qt::CopyAction && overlay && !overlay->isVisible();
    };
    if (!drop(QUrl::fromLocalFile(path), Qt::CopyAction | Qt::MoveAction) || pending.count() != 1) return false;
    if (!drop(QUrl::fromLocalFile(path), Qt::CopyAction) || pending.count() != 1) return false;
    drop(QUrl::fromLocalFile(directory.path()), Qt::CopyAction);
    if (pending.count() != 1) return false;
    if (drop(QUrl::fromLocalFile(path), Qt::MoveAction) || pending.count() != 1) return false;
    bool loaded = false;
    // ListView에서 따로 소유하는 대리 항목까지 실제 화면 트리로 찾는 함수.
    const auto findIcon = [](auto&& self, QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("pendingFileIcon")) return item;
        for (auto* child : item->childItems()) if (auto* found = self(self, child)) return found;
        return nullptr;
    };
    QEventLoop images;
    QTimer check;
    QTimer deadline;
    deadline.setSingleShot(true);
    // 비동기 파일 아이콘이 실제 QML 이미지에 로딩되는지 확인하는 함수.
    QObject::connect(&check, &QTimer::timeout, &images, [&] {
        auto* icon = findIcon(findIcon, page);
        if (icon && icon->property("status").toInt() == 1) { loaded = true; images.quit(); }
    });
    QObject::connect(&deadline, &QTimer::timeout, &images, &QEventLoop::quit);
    check.start(10);
    deadline.start(3000);
    images.exec();
    if (!loaded) {
        auto* icon = findIcon(findIcon, page);
        QTextStream(stderr) << "Pending icon did not load: " << (icon ? icon->property("status").toInt() : -1) << Qt::endl;
    }
    pending.removeAt(0);
    // 실제 이미지 내용의 축소와 손상된 그림의 아이콘 대체를 검사한다.
    const auto picturePath = directory.filePath(QStringLiteral("picture.png"));
    QImage picture(80, 40, QImage::Format_RGB32);
    picture.fill(Qt::red);
    if (!picture.save(picturePath)) return false;
    nookbit::FilePreviewProvider previews;
    QSize previewSize;
    const auto thumbnail = previews.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(picturePath)), &previewSize, QSize(48, 48));
    if (thumbnail.size() != QSize(48, 24) || thumbnail.pixelColor(24, 12) != QColor(Qt::red)) return false;
    const auto brokenPath = directory.filePath(QStringLiteral("broken.png"));
    QFile broken(brokenPath);
    if (!broken.open(QIODevice::WriteOnly)) return false;
    broken.write("not an image");
    broken.close();
    if (previews.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(brokenPath)), nullptr, QSize(48, 48)).isNull()) return false;
    if (!loaded || !drop(QUrl::fromLocalFile(picturePath), Qt::CopyAction) || pending.count() != 1) return false;
    loaded = false;
    check.start(10);
    deadline.start(3000);
    images.exec();
    if (!verifyInlineFilePreview(window, page)) {
        QTextStream(stderr) << "Inline file preview navigation failed." << Qt::endl;
        return false;
    }
    pending.removeAt(0);
    return loaded && pending.count() == 0 && fixture.open(QIODevice::ReadOnly) && fixture.readAll() == "unchanged";
}

// 소수 기준점의 반복 위치 저장이 바탕화면 좌표를 밀어내지 않는지 검사하는 함수.
static bool verifyCharacterAnchorStability(QQuickWindow* window, nookbit::CharacterSettingsController& settings,
    nookbit::CharacterWindowController& placement) {
    if (!window) return false;
    const QPoint original(placement.anchorX(), placement.anchorY());
    const auto savedPivot = settings.pivotOffset();
    const auto savedBubble = settings.bubbleOffset();
    bool stable = true;
    for (const auto offset : {QPointF(0.5, 1.0), QPointF(-7.25, 12.5), QPointF(19.5, -8.25)}) {
        settings.setPlacement(offset, savedBubble);
        QCoreApplication::processEvents();
        for (int i = 0; i < 8; ++i) {
            QMetaObject::invokeMethod(window, "commitPosition");
            QCoreApplication::processEvents();
        }
        stable = stable && QPoint(placement.anchorX(), placement.anchorY()) == original;
    }
    settings.setPlacement(savedPivot, savedBubble);
    return stable;
}

// 응용 프로그램과 의존성을 조립하고 이벤트 루프를 실행하는 함수.
int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setOrganizationName(QStringLiteral("NookBitProject"));
    QCoreApplication::setApplicationName(QStringLiteral("NookBit"));
    const bool verifyUi = QCoreApplication::arguments().contains(QStringLiteral("--verify-ui"));
    application.setWindowIcon(applicationIcon());
    if (verifyUi) {
        // 내장 아이콘의 모든 크기가 읽히고 바깥 모서리의 투명도가 유지되는지 검사한다.
        const auto icon = application.windowIcon();
        for (const int size : {16, 24, 32, 48, 64, 128, 256}) {
            const auto image = icon.pixmap(QSize(size, size)).toImage();
            if (!icon.availableSizes().contains(QSize(size, size)) || image.isNull() ||
                !image.hasAlphaChannel() || image.pixelColor(0, 0).alpha() != 0) {
                QTextStream(stderr) << "Bundled application icon failed at size " << size << Qt::endl;
                return EXIT_FAILURE;
            }
        }
    }
    // 숨긴 메인 창과 도구 창의 개수로 프로세스 수명을 결정하지 않는다.
    application.setQuitOnLastWindowClosed(false);
    nookbit::AppLifecycleController appLifecycle;
    nookbit::UserGuideController userGuides;

    nookbit::PendingFilesModel pendingFiles;
    const auto appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto fallbackDestination = QDir(documents).filePath(QStringLiteral("NookBit Organized"));
    QSettings uiPreferences;
    nookbit::UiSettingsController uiSettings(uiPreferences, !verifyUi);
    const auto rememberedDestination = verifyUi
        ? QString() : uiPreferences.value(QStringLiteral("destinationRoot")).toString().trimmed();
    const auto developmentDestination = QString::fromUtf8(NOOKBIT_DEV_DESTINATION_ROOT);
    // 로컬 개발 빌드에서 고정한 경로가 있으면 사용자 기억값보다 우선하는 기본 경로.
    const QFileInfo rememberedInfo(developmentDestination.isEmpty()
        ? rememberedDestination : developmentDestination);
    const auto defaultDestination = rememberedInfo.isAbsolute() &&
                                    !QDir(rememberedInfo.absoluteFilePath()).isRoot()
        ? rememberedInfo.absoluteFilePath() : fallbackDestination;
    if (QCoreApplication::arguments().contains(QStringLiteral("--print-default-destination"))) {
        QTextStream(stdout) << QDir::toNativeSeparators(defaultDestination) << Qt::endl;
        return EXIT_SUCCESS;
    }
    nookbit::JsonPendingFilesRepository pendingStorage(
        QDir(appData).filePath(QStringLiteral("pending-files.json")));
    const nookbit::IPendingFilesRepository& pendingRepository = pendingStorage;
    if (!verifyUi) {
        const auto restored = pendingRepository.load();
        if (restored) {
            QStringList paths;
            for (const auto& path : restored.value()) paths.append(QString::fromStdWString(path.wstring()));
            pendingFiles.restorePaths(paths);
            // 등록·제외·이동 완료로 목록이 바뀔 때 경로만 저장하는 함수.
            QObject::connect(&pendingFiles, &nookbit::PendingFilesModel::countChanged,
                             &application, [&pendingFiles, &pendingRepository] {
                std::vector<std::filesystem::path> paths;
                for (const auto& path : pendingFiles.filePaths()) paths.emplace_back(path.toStdWString());
                const auto saved = pendingRepository.save(paths);
                pendingFiles.setStorageNotice(saved ? QString() :
                    QStringLiteral("대기 목록을 저장하지 못했어요. 이번 변경은 재시작 후 복원되지 않을 수 있어요: %1")
                        .arg(QString::fromStdString(saved.error().message)));
            });
        } else {
            pendingFiles.setStorageNotice(QStringLiteral("대기 목록을 복원하지 못했어요. 기존 저장 파일은 보존하며 이번 실행의 목록 변경은 저장하지 않아요: %1")
                .arg(QString::fromStdString(restored.error().message)));
        }
    }
    QTimer pendingAvailabilityTimer;
    pendingAvailabilityTimer.setInterval(3000);
    QObject::connect(&pendingAvailabilityTimer, &QTimer::timeout,
                     &pendingFiles, &nookbit::PendingFilesModel::refreshAvailability);
    if (!verifyUi) pendingAvailabilityTimer.start();
    const auto databasePath = QDir(appData).filePath(QStringLiteral("history.sqlite"));
    const auto migrationPath = QStringLiteral(":/nookbit/001_initial.sql");
    nookbit::SqliteTransferJournal transferJournal(databasePath, migrationPath);
    const auto treeSchema = QStringLiteral(":/nookbit/rules-tree-v3.schema.json");
    const auto legacyTreeSchema = QStringLiteral(":/nookbit/rules-tree.schema.json");
    const auto bundledTree = QStringLiteral(":/nookbit/default-v3.json");
    const auto rulesPath = verifyUi ? bundledTree
        : QDir(appData).filePath(QStringLiteral("rules.json"));
    nookbit::JsonRuleSetRepository previewRules(rulesPath, treeSchema, legacyTreeSchema);
    // 사용자 규칙이 아직 없다면 이전 기본 분류와 같은 v3 규칙을 처음 한 번 저장한다.
    if (!verifyUi && !QFileInfo::exists(rulesPath)) {
        nookbit::JsonRuleSetRepository defaults(bundledTree, treeSchema);
        const auto initial = defaults.load();
        if (initial) previewRules.save(initial.value());
    }
    nookbit::FilesystemScanner previewScanner;
    nookbit::RuleEngine previewRuleEngine;
    nookbit::TestRuleUseCase testRule(previewScanner, previewRuleEngine);
    nookbit::RuleEditorController ruleEditor(
        previewRules, previewRules, nullptr, &previewRules,
        verifyUi ? QString() : QDir(appData).filePath(QStringLiteral("rule-graph-layout.json")),
        &previewRules, &testRule, &previewRules);
    nookbit::WindowsStagedCopy transferCopy;
    nookbit::Planner previewPlanner(previewRuleEngine, std::filesystem::path(appData.toStdWString()), nookbit::CrossVolumeDrafts::Allow);
    nookbit::QtPlanHasher previewHasher;
    nookbit::CreatePreviewUseCase createPreview(
        previewRules, previewScanner, previewPlanner, &previewHasher, &transferCopy);
    nookbit::SqliteHistoryRepository history(databasePath, migrationPath);
    nookbit::FreezePreviewUseCase freezePreview(
        previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy);
    nookbit::VerifySavedPlanUseCase verifySavedPlan(
        previewRules, previewScanner, previewPlanner, previewHasher, history, &transferCopy);
    nookbit::ApprovePlanUseCase approvePlan(verifySavedPlan, history);
    nookbit::UserSettings currentSettings;
    currentSettings.homeRoot = std::filesystem::path(defaultDestination.toStdWString());
    nookbit::RuntimeSettingsRepository recoverySettings(currentSettings);
    nookbit::DestinationSettingsController destinationSettings(uiPreferences, defaultDestination, !verifyUi);
    nookbit::ThreadLocalOperations fileOperations(databasePath, migrationPath, rulesPath, treeSchema, legacyTreeSchema,
        std::filesystem::path(appData.toStdWString()), recoverySettings);
    nookbit::BackgroundTaskRunner backgroundTasks;
    nookbit::PreviewController previewController(
        pendingFiles,
        createPreview,
        freezePreview,
        verifySavedPlan,
        approvePlan,
        history,
        fileOperations,
        rulesPath,
        treeSchema,
        defaultDestination,
        nullptr, legacyTreeSchema);
    if (!verifyUi) {
        previewController.setCollisionPolicy(uiPreferences.value(
            QStringLiteral("collisionPolicy"), QStringLiteral("rename")).toString());
    }
    QObject::connect(&previewController, &nookbit::PreviewController::collisionPolicyChanged,
                     &application, [&uiPreferences, &previewController, verifyUi] {
        if (verifyUi) return;
        // 선택한 공통 충돌 정책을 사용자 설정에 저장하는 함수.
        uiPreferences.setValue(QStringLiteral("collisionPolicy"), previewController.collisionPolicy());
        uiPreferences.sync();
    });
    QObject::connect(&previewController, &nookbit::PreviewController::destinationRootAccepted,
                     &application, [&uiPreferences, verifyUi](const QString& root) {
        if (verifyUi) return;
        // 성공한 미리보기의 목적지만 다음 실행의 기본 경로로 기억하는 함수.
        uiPreferences.setValue(QStringLiteral("destinationRoot"), root);
        uiPreferences.sync();
    });
    // 화면의 공통 충돌 정책을 코어의 현재 설정 값으로 변환하는 함수.
    const auto currentPolicy = [&previewController] {
        const auto key = previewController.collisionPolicy();
        return key == "abort" ? nookbit::CollisionPolicy::Abort : key == "skip" ? nookbit::CollisionPolicy::Skip : nookbit::CollisionPolicy::Rename;
    };
    recoverySettings.setCollisionPolicy(currentPolicy());
    QObject::connect(&previewController, &nookbit::PreviewController::destinationRootAccepted, &application,
        [&recoverySettings](const QString& root) { recoverySettings.setHomeRoot(std::filesystem::path(root.toStdWString())); });
    QObject::connect(&previewController, &nookbit::PreviewController::collisionPolicyChanged, &application,
        [&recoverySettings, &currentPolicy] { recoverySettings.setCollisionPolicy(currentPolicy()); });
    // 사용자 허용 목록을 실행·복구의 설정 스냅샷에 함께 반영하는 함수.
    const auto applyDestinationPermissions = [&destinationSettings, &recoverySettings, &previewController] {
        std::vector<std::filesystem::path> roots;
        for (const auto& path : destinationSettings.allowedRoots()) roots.emplace_back(path.toStdWString());
        recoverySettings.setAllowedDestinationRoots(roots);
        previewController.setAllowedDestinationRoots(std::move(roots));
    };
    applyDestinationPermissions();
    QObject::connect(&destinationSettings, &nookbit::DestinationSettingsController::allowedRootsChanged,
                     &application, applyDestinationPermissions);
    QObject::connect(&previewController, &nookbit::PreviewController::destinationRootAccepted,
                     &destinationSettings, &nookbit::DestinationSettingsController::setHomeRoot);
    nookbit::RecoveryController recoveryController(history, nullptr, &transferJournal, &fileOperations);
    nookbit::HistoryController historyController(history, fileOperations);
    previewController.setTaskRunner(&backgroundTasks);
    previewController.setSortWorkflow(&fileOperations);
    historyController.setTaskRunner(&backgroundTasks);
    recoveryController.setTaskRunner(&backgroundTasks);
    QObject::connect(&historyController, &nookbit::HistoryController::rollbackAttempted,
                     &previewController, &nookbit::PreviewController::refreshLastUndoAfterHistoryRollback);
    QObject::connect(&historyController, &nookbit::HistoryController::recoveryRefreshRequested,
                     &recoveryController, &nookbit::RecoveryController::refreshRecoveryCases);
    QObject::connect(&previewController, &nookbit::PreviewController::recoveryReconcileRequested,
                     &recoveryController, &nookbit::RecoveryController::reconcileAndRefreshCases);
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &previewController, &nookbit::PreviewController::refreshActiveRules);
    QObject::connect(&recoveryController, &nookbit::RecoveryController::recoveryAttempted, &historyController,
        [&historyController, &pendingFiles](const QString&) { historyController.refreshHistory(); pendingFiles.refreshAvailability(); });
    QObject::connect(&recoveryController, &nookbit::RecoveryController::recoveryCompleted, &application,
        [&history, &pendingFiles, &previewController](const QString& id, const QString& action) {
            if (action == "rollback") { previewController.refreshLastUndoAfterHistoryRollback(id); return; }
            const auto plan = history.loadPlan(id.toStdString());
            if (!plan || plan.value().status != nookbit::PlanStatus::Completed) return;
            for (int i = pendingFiles.count() - 1; i >= 0; --i) {
                const std::filesystem::path path(pendingFiles.filePaths()[i].toStdWString());
                if (std::any_of(plan.value().steps.begin(), plan.value().steps.end(), [&](const nookbit::MoveStep& step) {
                    return step.status == nookbit::StepStatus::Applied && step.source == path;
                })) pendingFiles.removeAt(i);
            }
        });
    if (!verifyUi && QFileInfo::exists(databasePath)) {
        recoveryController.refreshRecoveryCases();
    }
    nookbit::JsonWatchSettingsRepository watchSettings(
        QDir(appData).filePath(QStringLiteral("watch.json")),
        QStringLiteral(":/nookbit/watch.schema.json"));
    nookbit::FilesystemScanner watchScanner;
    nookbit::RuleEngine watchRuleEngine;
    nookbit::EvaluateWatchFolderUseCase evaluateWatchFolder(
        watchScanner, watchRuleEngine, previewRules);
    nookbit::WatchController watchController(
        pendingFiles, watchSettings, evaluateWatchFolder,
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    nookbit::CharacterController characterController;
    const auto bundledCharacter = QStringLiteral(":/nookbit/character.presentation.json");
    const auto characterSchema = QStringLiteral(":/nookbit/character-presentation.schema.json");
    const auto customCharacter = QDir(appData).filePath(QStringLiteral("character.presentation.json"));
    auto characterPath = !verifyUi && QFileInfo::exists(customCharacter) ? customCharacter : bundledCharacter;
    auto characterPresentation = nookbit::loadCharacterPresentation(characterPath, characterSchema);
    if (!characterPresentation && !verifyUi) {
        QTextStream(stderr) << QString::fromStdString(characterPresentation.error().message) << Qt::endl;
        characterPath = bundledCharacter;
        characterPresentation = nookbit::loadCharacterPresentation(bundledCharacter, characterSchema);
    }
    if (!characterPresentation) return EXIT_FAILURE;
    const auto characterImageBase = characterPath.startsWith(QStringLiteral(":/"))
        ? QUrl(QStringLiteral("qrc") + QFileInfo(characterPath).path() + QStringLiteral("/"))
        : QUrl::fromLocalFile(QFileInfo(characterPath).absolutePath() + QStringLiteral("/"));
    const auto configuredCharacter = characterController.setPresentation(
        std::move(characterPresentation.value()), characterImageBase);
    if (!configuredCharacter) return EXIT_FAILURE;
    const nookbit::JsonImagePackLoader imagePackLoader;
    nookbit::DirectoryImagePackLibrary imagePackLibrary(imagePackLoader,
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("imagepacks/test/imagepack.json")),
        QDir(verifyUi ? QCoreApplication::applicationDirPath() : appData).filePath(
            verifyUi ? QStringLiteral("verify-unused-imagepacks") : QStringLiteral("imagepacks")),
        {{"builtin-nmoo", QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("imagepacks/nmoo/imagepack.json"))}});
    // 임포트 수명은 보관소보다 짧게 유지하고 파일 정리 실행기와 분리한다.
    nookbit::BackgroundTaskRunner characterImports;
    nookbit::CharacterSettingsController characterSettings(imagePackLibrary,
        [&characterController](nookbit::ImagePack pack) {
            // 값으로 전달된 팩을 캐릭터 표현 창구에 적용하는 함수.
            return characterController.setImagePack(std::move(pack));
        }, characterImports);
    characterSettings.refresh();
    const auto savedPack = verifyUi ? QString() : uiPreferences.value(QStringLiteral("character/selectedPack")).toString();
    if (!savedPack.isEmpty() && !characterSettings.selectPack(savedPack))
        QTextStream(stderr) << characterSettings.status() << Qt::endl;
    if (characterSettings.selectedKey().isEmpty() && (verifyUi || characterPath == bundledCharacter)) {
        if (!characterSettings.selectPack(QStringLiteral("builtin")) && verifyUi) return EXIT_FAILURE;
    }
    if (!verifyUi) characterSettings.setUserScale(uiPreferences.value(
        QStringLiteral("character/scales/") + characterSettings.selectedKey(), 0.5).toDouble());
    // 선택한 팩과 개인 배율을 사용자 설정에서 복원하는 함수.
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::selected, &application,
        [&uiPreferences, &characterSettings, verifyUi](const QString& key) {
            if (verifyUi) return;
            characterSettings.setUserScale(uiPreferences.value(QStringLiteral("character/scales/") + key, 0.5).toDouble());
            uiPreferences.setValue(QStringLiteral("character/selectedPack"), key);
            uiPreferences.sync();
        });
    // 개인 배율 변경만 저장하고 팩 매니페스트는 수정하지 않는 함수.
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::scaleChanged, &application,
        [&uiPreferences, &characterSettings, verifyUi] {
            if (verifyUi || characterSettings.selectedKey().isEmpty()) return;
            uiPreferences.setValue(QStringLiteral("character/scales/") + characterSettings.selectedKey(), characterSettings.userScale());
            uiPreferences.sync();
        });
    // 화면의 작업 표시줄을 제외한 사용 가능 영역을 수집하는 함수.
    // 팩별 개인 좌표를 복원하며 다른 팩의 보정값을 가져오지 않는 함수.
    const auto restoreCharacterPlacement = [&uiPreferences, &characterSettings, verifyUi] {
        const auto prefix = QStringLiteral("character/placement/") + characterSettings.selectedKey();
        characterSettings.setPlacement(verifyUi ? QPointF() : uiPreferences.value(prefix + "/pivot", QPointF()).toPointF(),
            verifyUi ? QPointF() : uiPreferences.value(prefix + "/bubble", QPointF()).toPointF());
    };
    restoreCharacterPlacement();
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::selected, &application,
        restoreCharacterPlacement);
    // 적용한 좌표만 개인 설정에 저장하고 팩 파일은 보존하는 함수.
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::placementChanged, &application,
        [&uiPreferences, &characterSettings, verifyUi] {
            if (verifyUi || characterSettings.selectedKey().isEmpty()) return;
            const auto prefix = QStringLiteral("character/placement/") + characterSettings.selectedKey();
            uiPreferences.setValue(prefix + "/pivot", characterSettings.pivotOffset());
            uiPreferences.setValue(prefix + "/bubble", characterSettings.bubbleOffset());
            uiPreferences.sync();
        });
    // 저장 위치는 조립부에서 제공하고 편집 담당자는 레지스트리를 직접 알지 않는다.
    QMap<QString, QVariantMap> verificationEdits;
    nookbit::CompanionEditController companionEdits(
        [&characterController](const QVariantMap& edits) { return characterController.validateEdits(edits); },
        [&characterController](const QVariantMap& edits) { return characterController.setEdits(edits); },
        [&uiPreferences, &verificationEdits, verifyUi](const QString& key) -> nookbit::Result<QVariantMap> {
            // 팩별 개인 편집 문서를 읽고 저장 형식을 검증하는 함수.
            if (verifyUi) return nookbit::Result<QVariantMap>::success(verificationEdits.value(key));
            const auto bytes = uiPreferences.value(QStringLiteral("character/edits/") + key).toByteArray();
            if (bytes.isEmpty()) return nookbit::Result<QVariantMap>::success({});
            const auto document = QJsonDocument::fromJson(bytes);
            const auto root = document.object();
            if (!document.isObject() || root.value("schemaVersion").toInt() != 1 || !root.value("edits").isObject())
                return nookbit::Result<QVariantMap>::failure({"character.invalid_saved_edits", "저장된 컴패니언 편집값을 읽지 못했어요. 팩 기본값을 사용해요."});
            return nookbit::Result<QVariantMap>::success(root.value("edits").toObject().toVariantMap());
        },
        [&uiPreferences, &verificationEdits, verifyUi](const QString& key, const QVariantMap& edits) -> nookbit::Result<void> {
            // 개인 편집값만 저장하고 저장 오류 시 이전 설정을 복구하는 함수.
            if (verifyUi) { verificationEdits.insert(key, edits); return nookbit::Result<void>::success(); }
            const auto path = QStringLiteral("character/edits/") + key;
            const bool existed = uiPreferences.contains(path);
            const auto previous = uiPreferences.value(path);
            const auto document = QJsonDocument::fromVariant(QVariantMap{{"schemaVersion", 1}, {"edits", edits}});
            uiPreferences.setValue(path, document.toJson(QJsonDocument::Compact));
            uiPreferences.sync();
            if (uiPreferences.status() == QSettings::NoError) return nookbit::Result<void>::success();
            if (existed) uiPreferences.setValue(path, previous); else uiPreferences.remove(path);
            uiPreferences.sync();
            return nookbit::Result<void>::failure({"character.save_edits_failed", "컴패니언 편집값을 저장하지 못했어요. 변경 내용은 초안에 남아 있어요."});
        });
    companionEdits.loadPack(characterSettings.selectedKey());
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::selected, &companionEdits,
        &nookbit::CompanionEditController::loadPack);
    const auto characterScreens = [] {
        QList<QRect> areas;
        for (const auto* screen : QGuiApplication::screens()) areas.append(screen->availableGeometry());
        return areas;
    };
    const auto screens = characterScreens();
    const auto initialAnchor = screens.isEmpty() ? QPoint(200, 200)
        : QPoint(screens.first().right() - 180, screens.first().bottom() - 140);
    nookbit::CharacterWindowController characterWindow(screens,
        verifyUi ? initialAnchor : uiPreferences.value(QStringLiteral("character/windowAnchor"), initialAnchor).toPoint(),
        verifyUi || uiPreferences.value(QStringLiteral("character/windowShown"), true).toBool());
    // 독립 창 위치와 노출만 사용자 설정에 저장하는 함수.
    const auto saveCharacterWindow = [&uiPreferences, &characterWindow, verifyUi] {
        if (verifyUi) return;
        uiPreferences.setValue(QStringLiteral("character/windowAnchor"), QPoint(characterWindow.anchorX(), characterWindow.anchorY()));
        uiPreferences.setValue(QStringLiteral("character/windowShown"), characterWindow.shown());
        uiPreferences.sync();
    };
    QTimer characterPositionSave;
    characterPositionSave.setSingleShot(true);
    characterPositionSave.setInterval(180);
    QObject::connect(&characterPositionSave, &QTimer::timeout, &application, saveCharacterWindow);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::positionChanged, &characterPositionSave,
        [&characterPositionSave] {
            // 드래그 중 좌표 변경은 즉시 반영하되 디스크 저장은 잠깐 묶는 함수.
            characterPositionSave.start();
        });
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &application, saveCharacterWindow);
    QObject::connect(&application, &QCoreApplication::aboutToQuit, &application, saveCharacterWindow);
    // 모니터 추가·제거와 작업 영역 변경 시 보이지 않는 위치를 복원하는 함수.
    const auto refreshCharacterScreens = [&characterWindow, characterScreens] { characterWindow.setScreens(characterScreens()); };
    QObject::connect(&application, &QGuiApplication::screenRemoved, &characterWindow, refreshCharacterScreens);
    QObject::connect(&application, &QGuiApplication::screenAdded, &characterWindow,
        [&characterWindow, refreshCharacterScreens](QScreen* screen) {
            // 새 화면의 작업 영역 변경도 같은 위치 복원 창구에 연결하는 함수.
            QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
            refreshCharacterScreens();
        });
    for (auto* screen : QGuiApplication::screens())
        QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
    nookbit::InteractionManager interactionManager;
    const auto interactionsRegistered = nookbit::registerBasicCompanionInteractions(interactionManager);
    if (!interactionsRegistered) return EXIT_FAILURE;
    nookbit::CompanionInteractionController companionInteractions(interactionManager, "primary", [&characterController] {
        // 입력 당시 기본 행동과 일시 반응을 값으로만 전달하는 함수.
        return nookbit::InteractionContext{characterController.behaviorId().toStdString(), characterController.reactionId().toStdString()};
    });
    QObject::connect(&companionInteractions, &nookbit::CompanionInteractionController::reactionRequested,
        &characterController, &nookbit::CharacterController::presentInteraction);
    const auto refreshInteractionTarget = [&companionInteractions, &characterWindow] {
        // 바탕화면 표시 여부만 입력 가능 상태에 반영하는 함수.
        companionInteractions.setAvailable(characterWindow.shown());
    };
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &companionInteractions, refreshInteractionTarget);
    QObject::connect(&characterSettings, &nookbit::CharacterSettingsController::selected, &companionInteractions,
        [&companionInteractions](const QString&) {
            // 새 팩에 이전 상호작용 반응을 이어서 표시하지 않는 함수.
            companionInteractions.cancel();
        });
    refreshInteractionTarget();
    for (const auto& behavior : {nookbit::CompanionBehavior{"walking.left", "walk.left", 5, {"walk.left"}, {"working", "attention"}},
                               nookbit::CompanionBehavior{"walking.right", "walk.right", 5, {"walk.right"}, {"working", "attention"}}}) {
        if (!characterController.registerBehavior(behavior)) return EXIT_FAILURE;
    }
    nookbit::IBehaviorChannel& behaviorChannel = characterController;
    nookbit::CompanionMotionController companionMotion(
        [&characterController, &characterWindow] {
            // 컴패니언의 현재 상황과 화면 범위만 모션 모듈에 값으로 전달하는 함수.
            const auto behavior = characterController.behaviorId();
            const auto area = characterWindow.screenArea();
            const bool idleOrWalking = behavior == "resting" || behavior == "walking.left" || behavior == "walking.right";
            return nookbit::IdleWalkContext{characterWindow.shown() && characterController.canWalk() && idleOrWalking && characterController.reactionId().isEmpty(),
                static_cast<double>(characterWindow.anchorX()), static_cast<double>(area.left() + 32), static_cast<double>(area.right() - 32)};
        },
        [&behaviorChannel, &characterWindow](const nookbit::IdleWalkStep& step) {
            // 모션 출력만 행동 창구와 창 위치에 연결하고 파일 작업에는 관여하지 않는 함수.
            const auto accepted = behaviorChannel.publish({"companion.motion", "idle-walk", {step.direction < 0 ? "walk.left" : "walk.right"}, step.walking});
            if (!accepted) { QTextStream(stderr) << QString::fromStdString(accepted.error().message) << Qt::endl; return; }
            if (step.move) characterWindow.moveAnchor(static_cast<int>(std::round(step.x)), characterWindow.anchorY());
        }, !verifyUi && uiPreferences.value(QStringLiteral("character/autonomousWalk"), true).toBool());
    QObject::connect(&characterController, &nookbit::CharacterController::stateChanged, &companionMotion, &nookbit::CompanionMotionController::refresh);
    QObject::connect(&characterController, &nookbit::CharacterController::reactionChanged, &companionMotion, &nookbit::CompanionMotionController::refresh);
    QObject::connect(&characterController, &nookbit::CharacterController::previewAnimationsChanged, &companionMotion, &nookbit::CompanionMotionController::restart);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &companionMotion, &nookbit::CompanionMotionController::refresh);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::screenAreaChanged, &companionMotion, &nookbit::CompanionMotionController::refresh);
    QObject::connect(&companionMotion, &nookbit::CompanionMotionController::enabledChanged, &application,
        [&uiPreferences, &companionMotion, verifyUi] {
            // 사용자의 자율 걷기 선택만 자동 저장하는 함수.
            if (verifyUi) return;
            uiPreferences.setValue(QStringLiteral("character/autonomousWalk"), companionMotion.enabled());
            uiPreferences.sync();
        });
    companionMotion.refresh();
    // 알림 표시 설정만 저장하며 감시 기능이나 작업 기록에는 관여하지 않는 창구.
    QMap<QString, bool> verificationNotificationFlags;
    nookbit::NotificationController notifications(
        [&uiPreferences, &verificationNotificationFlags, verifyUi](const QString& id, bool fallback) {
            // 등록된 알림 종류의 사용자 표시 설정을 읽는 함수.
            return verifyUi ? verificationNotificationFlags.value(id, fallback)
                : uiPreferences.value(QStringLiteral("notifications/types/") + id + "/enabled", fallback).toBool();
        },
        [&uiPreferences, &verificationNotificationFlags, verifyUi](const QString& id, bool value) -> nookbit::Result<void> {
            // 표시 설정만 저장하고 실패하면 기존 설정을 보존하는 함수.
            if (verifyUi) { verificationNotificationFlags.insert(id, value); return nookbit::Result<void>::success(); }
            const auto key = QStringLiteral("notifications/types/") + id + "/enabled";
            const bool existed = uiPreferences.contains(key);
            const auto previous = uiPreferences.value(key);
            uiPreferences.setValue(key, value);
            uiPreferences.sync();
            if (uiPreferences.status() == QSettings::NoError) return nookbit::Result<void>::success();
            if (existed) uiPreferences.setValue(key, previous); else uiPreferences.remove(key);
            uiPreferences.sync();
            return nookbit::Result<void>::failure({"notification.save_failed", "알림 설정을 저장하지 못했어요. 이전 설정을 유지해요."});
        });
    const auto registeredNotice = notifications.registerType({"file.offer", "folder-watch", "폴더 감시",
        "정리 제안", "감시 폴더에서 정리할 파일이 발견되면 알려줘요.", true});
    if (!registeredNotice) { QTextStream(stderr) << QString::fromStdString(registeredNotice.error().message) << Qt::endl; return EXIT_FAILURE; }
    nookbit::ICharacterChannel& characterChannel = characterController;
    // 표시가 허용된 알림을 컴패니언 표현 창구에만 연결하는 조립부 어댑터.
    QObject::connect(&notifications, &nookbit::NotificationController::notificationChanged, &characterController,
        [&characterChannel](const QString& typeId, const QString& sourceId, const QString& instanceId,
            bool active, const QString&, const QString& message) {
            const auto received = characterChannel.publish({sourceId.toStdString(), (typeId + "/" + instanceId).toStdString(),
                nookbit::CharacterUpdateKind::Attention, active, message.toStdString(), typeId.toStdString()});
            if (!received) QTextStream(stderr) << QString::fromStdString(received.error().message) << Qt::endl;
        });
    // 파일 작업 상태를 공통 입력으로 변환해 인터페이스 창구에 전달하는 함수.
    const auto reportCharacterActivity = [&characterChannel, &backgroundTasks] {
        const auto received = characterChannel.publish({"file-operations", "current-operation",
            nookbit::CharacterUpdateKind::Activity, backgroundTasks.fileWorkActive(), {}, "file.work"});
        if (!received) QTextStream(stderr) << QString::fromStdString(received.error().message) << Qt::endl;
    };
    nookbit::INotificationChannel& notificationChannel = notifications;
    // 감시 기능의 제안 상태를 컴패니언을 모르는 공통 알림 창구로 전달하는 함수.
    const auto reportWatchNotification = [&notificationChannel, &watchController] {
        const auto received = notificationChannel.publish({"file.offer", "organization-offer",
            watchController.proposalVisible(), "정리 제안", "정리할 파일이 있어요!"});
        if (!received) QTextStream(stderr) << QString::fromStdString(received.error().message) << Qt::endl;
    };
    // 작업 상태를 값으로 전달하고 캐릭터가 다른 컨트롤러를 호출하지 않게 연결하는 함수.
    QObject::connect(&backgroundTasks, &nookbit::BackgroundTaskRunner::busyChanged,
        &characterController, reportCharacterActivity);
    // 감시 제안의 생성·수락·나중에 처리 결과를 공통 알림 창구에 전달하는 함수.
    QObject::connect(&watchController, &nookbit::WatchController::watchChanged,
        &notifications, reportWatchNotification);
    reportCharacterActivity();
    reportWatchNotification();
    // 제안 대기 시각을 같은 감시 폴더에 한해 사용자 설정에 저장하는 함수.
    QObject::connect(&watchController, &nookbit::WatchController::snoozeDeadlineChanged,
        &application, [&uiPreferences, &watchController, verifyUi](qint64 deadline) {
            if (verifyUi) return;
            uiPreferences.setValue(QStringLiteral("watchSnoozeFolder"), watchController.folder());
            uiPreferences.setValue(QStringLiteral("watchSnoozeUntilUtcMs"), deadline);
            uiPreferences.sync();
        });
    if (!verifyUi && QString::compare(
            QDir::cleanPath(uiPreferences.value(QStringLiteral("watchSnoozeFolder")).toString()),
            QDir::cleanPath(watchController.folder()), Qt::CaseInsensitive) == 0) {
        watchController.restoreSnoozeUntil(
            uiPreferences.value(QStringLiteral("watchSnoozeUntilUtcMs"), 0).toLongLong());
    }
    QObject::connect(&ruleEditor, &nookbit::RuleEditorController::rulesSaved,
                     &watchController, &nookbit::WatchController::rulesChanged);
    QQmlApplicationEngine engine;
    auto* uiTheme = engine.singletonInstance<QObject*>("NookBit", "UiTheme");
    if (!uiTheme) {
        QTextStream(stderr) << "Cannot load shared UI theme." << Qt::endl;
        return EXIT_FAILURE;
    }
    const auto lightWidgetPalette = application.palette();
    // 네이티브 메뉴도 QML 공통 색상표에서 읽어 같은 테마를 적용하는 함수.
    const auto applyWidgetTheme = [&application, &uiSettings, uiTheme, lightWidgetPalette] {
        const auto colors = uiTheme->property(uiSettings.darkMode() ? "dark" : "light").value<QJSValue>().toVariant().toMap();
        QPalette palette = lightWidgetPalette;
        palette.setColor(QPalette::Window, QColor(colors.value("window").toString()));
        palette.setColor(QPalette::Base, QColor(colors.value("surface").toString()));
        palette.setColor(QPalette::AlternateBase, QColor(colors.value("alternate").toString()));
        palette.setColor(QPalette::Button, QColor(colors.value("button").toString()));
        palette.setColor(QPalette::Mid, QColor(colors.value("border").toString()));
        for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
            palette.setColor(role, QColor(colors.value("text").toString()));
            palette.setColor(QPalette::Disabled, role, QColor(colors.value("muted").toString()));
        }
        palette.setColor(QPalette::Highlight, QColor(colors.value("accent").toString()));
        palette.setColor(QPalette::HighlightedText, QColor(colors.value("accentText").toString()));
        application.setPalette(palette);
    };
    applyWidgetTheme();
    QObject::connect(&uiSettings, &nookbit::UiSettingsController::darkModeChanged, &application, applyWidgetTheme);
    engine.addImageProvider(QStringLiteral("fileicons"), new nookbit::FileIconProvider);
    engine.addImageProvider(QStringLiteral("filepreviews"), new nookbit::FilePreviewProvider);
    // QML 공통 규격을 네이티브 메뉴·대화상자의 기본 글씨에도 적용한다.
    auto* uiMetrics = engine.singletonInstance<QObject*>("NookBit", "UiMetrics");
    if (!uiMetrics || uiMetrics->property("textBody").toInt() <= 0) {
        QTextStream(stderr) << "Cannot load shared UI metrics." << Qt::endl;
        return 1;
    }
    QFont baseFont = application.font();
    const auto previousFont = QFontInfo(baseFont).family();
    baseFont.setFamilies({uiMetrics->property("fontFamily").toString(),
        uiMetrics->property("fontFallback").toString(), baseFont.family()});
    baseFont.setPixelSize(uiMetrics->property("textBody").toInt());
    application.setFont(baseFont);
    if (verifyUi) QTextStream(stdout) << "UI font: " << previousFont << " -> " << QFontInfo(baseFont).family() << Qt::endl;
    engine.rootContext()->setContextProperty(QStringLiteral("pendingFiles"), &pendingFiles);
    engine.rootContext()->setContextProperty(QStringLiteral("backgroundTasks"), &backgroundTasks);
    engine.rootContext()->setContextProperty(QStringLiteral("previewController"), &previewController);
    engine.rootContext()->setContextProperty(QStringLiteral("recoveryController"), &recoveryController);
    engine.rootContext()->setContextProperty(QStringLiteral("historyController"), &historyController);
    engine.rootContext()->setContextProperty(QStringLiteral("watchController"), &watchController);
    engine.rootContext()->setContextProperty(QStringLiteral("characterController"), &characterController);
    engine.rootContext()->setContextProperty(QStringLiteral("notifications"), &notifications);
    engine.rootContext()->setContextProperty(QStringLiteral("companionInteractions"), &companionInteractions);
    engine.rootContext()->setContextProperty(QStringLiteral("companionMotion"), &companionMotion);
    engine.rootContext()->setContextProperty(QStringLiteral("characterSettings"), &characterSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("companionEdits"), &companionEdits);
    engine.rootContext()->setContextProperty(QStringLiteral("characterWindow"), &characterWindow);
    engine.rootContext()->setContextProperty(QStringLiteral("appLifecycle"), &appLifecycle);
    engine.rootContext()->setContextProperty(QStringLiteral("uiSettings"), &uiSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("destinationSettings"), &destinationSettings);
    engine.rootContext()->setContextProperty(QStringLiteral("ruleEditor"), &ruleEditor);
    engine.rootContext()->setContextProperty(QStringLiteral("userGuides"), &userGuides);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 트레이 요청을 화면·캐릭터 상태에 연결하는 일은 조립부에서만 처리한다.
    QMenu trayMenu;
    auto* openAction = trayMenu.addAction(QStringLiteral("NookBit 열기"));
    auto* characterAction = trayMenu.addAction(QStringLiteral("캐릭터 표시"));
    characterAction->setCheckable(true);
    characterAction->setChecked(characterWindow.shown());
    trayMenu.addSeparator();
    auto* exitAction = trayMenu.addAction(QStringLiteral("완전 종료"));
    QSystemTrayIcon tray(application.windowIcon());
    tray.setToolTip(QStringLiteral("NookBit · 창을 닫아도 실행 중"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(openAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::openWindow);
    QObject::connect(exitAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::requestExit);
    QObject::connect(characterAction, &QAction::triggered, &characterWindow, &nookbit::CharacterWindowController::setShown);
    QObject::connect(&characterWindow, &nookbit::CharacterWindowController::visibilityChanged, &trayMenu,
        [&characterWindow, characterAction] {
            // 캐릭터 설정과 트레이 메뉴의 표시 상태를 일치시키는 함수.
            characterAction->setChecked(characterWindow.shown());
        });
    QObject::connect(&tray, &QSystemTrayIcon::activated, &appLifecycle,
        [&appLifecycle](QSystemTrayIcon::ActivationReason reason) {
            // 왼쪽 클릭 또는 더블클릭으로 숨긴 메인 창을 복원하는 함수.
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) appLifecycle.openWindow();
        });
    if (!engine.rootObjects().isEmpty()) {
        auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::hideRequested, mainWindow, &QWindow::hide);
        QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::showRequested, mainWindow, [mainWindow] {
            // 창을 다시 표시하고 키보드 입력을 받을 수 있도록 활성화하는 함수.
            mainWindow->showNormal(); mainWindow->raise(); mainWindow->requestActivate();
        });
    }
    QObject::connect(&appLifecycle, &nookbit::AppLifecycleController::exitRequested, &application,
        [&engine, &uiPreferences] {
            // 마지막 캐릭터 위치를 확정한 후 설정을 반영하고 완전히 종료하는 함수.
            if (!engine.rootObjects().isEmpty()) QMetaObject::invokeMethod(engine.rootObjects().first(), "commitCharacterPosition");
            uiPreferences.sync();
            QCoreApplication::quit();
        });
    // 종료는 막되 창 숨기기는 허용하도록 모든 백그라운드 작업 상태를 합치는 함수.
    const auto refreshExitState = [&appLifecycle, &backgroundTasks, &characterImports, exitAction] {
        const bool busy = backgroundTasks.busy() || characterImports.busy();
        appLifecycle.setBusy(busy);
        exitAction->setEnabled(!busy);
        exitAction->setText(busy ? QStringLiteral("작업 중 · 완료 후 종료 가능") : QStringLiteral("완전 종료"));
    };
    QObject::connect(&backgroundTasks, &nookbit::BackgroundTaskRunner::busyChanged, &appLifecycle, refreshExitState);
    QObject::connect(&characterImports, &nookbit::BackgroundTaskRunner::busyChanged, &appLifecycle, refreshExitState);
    refreshExitState();
    // 시스템 트레이가 없어졌을 때 숨긴 앱이 접근 불가능해지지 않도록 확인하는 함수.
    const auto refreshTray = [&appLifecycle, &tray, verifyUi] {
        const bool available = !verifyUi && QSystemTrayIcon::isSystemTrayAvailable();
        appLifecycle.setTrayAvailable(available);
        if (available && !tray.isVisible()) tray.show();
        else if (!available && tray.isVisible()) tray.hide();
    };
    refreshTray();
    QTimer trayAvailability;
    trayAvailability.setInterval(15000);
    QObject::connect(&trayAvailability, &QTimer::timeout, &appLifecycle, refreshTray);
    if (!verifyUi) trayAvailability.start();

    // 자동 검사에서는 화면과 리소스를 확인하고 이벤트를 한 번 처리한 뒤 종료한다.
    if (verifyUi) {
        if (!verifyBundledHistorySchema(migrationPath)) return EXIT_FAILURE;
        bool compactLayout = false;
        if (!engine.rootObjects().isEmpty()) {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            auto* home = window ? window->findChild<QObject*>(QStringLiteral("compactHome")) : nullptr;
            auto* developer = window ? window->findChild<QObject*>(QStringLiteral("developerTools")) : nullptr;
            auto* extension = window ? window->findChild<QQuickWindow*>(QStringLiteral("extensionWindow")) : nullptr;
            if (window && home && developer && extension) {
                // 다른 창의 배치·노출 문제를 검사 단계 이름으로 식별하는 함수.
                const auto checkLayout = [&compactLayout](bool condition, const char* stage) {
                    if (!condition) { compactLayout = false; QTextStream(stderr) << "Layout check failed: " << stage << Qt::endl; }
                };
                // 본체 크기를 유지하는 별도 창·프레임 제거·연결 위치·개발 도구 숨김을 검사한다.
                compactLayout = true;
                auto* rulePage = window->findChild<QQuickItem*>(QStringLiteral("ruleEditorPage"));
                auto* closeButton = window->findChild<QQuickItem*>(QStringLiteral("extensionCloseButton"));
                checkLayout(rulePage && rulePage->window() == extension, "rule page belongs to extension window");
                checkLayout(closeButton && closeButton->window() == extension, "close button belongs to extension window");
                checkLayout(window->width() == 380 && !developer->property("visible").toBool()
                    && window->flags().testFlag(Qt::FramelessWindowHint) && !extension->isVisible(), "initial");
                QMetaObject::invokeMethod(home, "pageRequested", Q_ARG(int, 1));
                checkLayout(window->width() == 380 && extension->isVisible()
                    && extension->transientParent() == window && extension->flags().testFlag(Qt::FramelessWindowHint), "open extension");
                // 페이지 전환 직전 크기를 저장하고 다시 열 때 복원하는지 검사한다.
                const QSize ruleSize(extension->minimumWidth(), extension->minimumHeight());
                extension->resize(ruleSize);
                QCoreApplication::processEvents();
                window->setProperty("currentPage", 8);
                QCoreApplication::processEvents();
                const QSize settingsSize(extension->minimumWidth(), extension->minimumHeight());
                extension->resize(settingsSize);
                QCoreApplication::processEvents();
                window->setProperty("currentPage", 1);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == ruleSize, "restore rule page size");
                window->setProperty("currentPage", 8);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == settingsSize && settingsSize != ruleSize, "independent settings size");
                window->setProperty("currentPage", 0);
                window->setProperty("currentPage", 1);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == ruleSize, "restore after closing extension");
                const auto previousHeight = window->height();
                window->setHeight(previousHeight + 10);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == ruleSize, "main height does not resize extension");
                window->setHeight(previousHeight);
                const auto previousX = window->x();
                window->setX(previousX + 12);
                QCoreApplication::processEvents();
                const auto expectedDock = window->x() + window->width() + 10;
                checkLayout(std::abs(extension->property("dockX").toDouble() - expectedDock) < 1, "follow position");
                // 음수 좌표와 다른 모니터 영역에서도 위치를 되돌려 가두지 않는지 검사한다.
                for (const int position : {-1400, 1500, 3500}) {
                    window->setX(position);
                    QCoreApplication::processEvents();
                    checkLayout(extension->x() == window->x() + window->width() + 10, "cross-monitor follow");
                }
                QMetaObject::invokeMethod(home, "pageRequested", Q_ARG(int, 7));
                QCoreApplication::processEvents();
                auto* filePage = window->findChild<QQuickItem*>(QStringLiteral("fileSortPage"));
                checkLayout(filePage && filePage->isVisible() && filePage->window() == extension, "file sorting is a separate page");
                // 파일 정리의 중단 작업 요청이 서브창 본문과 그 창의 확인 대화상자로 이어지는지 검사한다.
                QMetaObject::invokeMethod(filePage, "recoveryRequested");
                QCoreApplication::processEvents();
                // 읽기 전용 중단 검사 완료를 기다린 뒤 다른 화면 입력을 시험하는 함수.
                QEventLoop recoveryInspection;
                QObject::connect(&backgroundTasks, &nookbit::BackgroundTaskRunner::busyChanged, &recoveryInspection,
                    [&] { if (!backgroundTasks.busy()) recoveryInspection.quit(); });
                QTimer::singleShot(5000, &recoveryInspection, &QEventLoop::quit);
                if (backgroundTasks.busy()) recoveryInspection.exec();
                checkLayout(!backgroundTasks.busy(), "read-only recovery inspection completed");
                auto* recoveryPage = window->findChild<QQuickItem*>(QStringLiteral("recoveryDialog"));
                auto* recoveryConfirm = window->findChild<QObject*>(QStringLiteral("recoveryConfirmDialog"));
                checkLayout(window->property("currentPage").toInt() == 10 && recoveryPage
                    && recoveryPage->isVisible() && recoveryPage->window() == extension, "recovery is a subwindow page");
                if (recoveryConfirm) {
                    QMetaObject::invokeMethod(recoveryConfirm, "open");
                    QCoreApplication::processEvents();
                    auto* content = qobject_cast<QQuickItem*>(recoveryConfirm->property("contentItem").value<QObject*>());
                    checkLayout(content && content->window() == extension, "recovery confirmation belongs to subwindow");
                    QMetaObject::invokeMethod(recoveryConfirm, "close");
                } else checkLayout(false, "recovery confirmation exists");
                window->setProperty("currentPage", 7);
                // 복원된 창 크기에 맞춰 드롭 영역의 실제 레이아웃 프레임을 갱신한다.
                QEventLoop restoredLayout;
                QTimer::singleShot(40, &restoredLayout, &QEventLoop::quit);
                restoredLayout.exec();
                checkLayout(verifyPageFileDrop(extension, filePage, pendingFiles), "whole-page drop and file icon");
                auto* undoButton = window->findChild<QQuickItem*>(QStringLiteral("compactUndoButton"));
                checkLayout(undoButton && undoButton->width() >= undoButton->implicitWidth() && undoButton->width() >= 60, "undo label width");
                auto* previewButton = window->findChild<QQuickItem*>(QStringLiteral("compactPreviewButton"));
                auto* sortButton = window->findChild<QQuickItem*>(QStringLiteral("compactSortButton"));
                checkLayout(previewButton && sortButton && undoButton
                    && std::abs(previewButton->width() - sortButton->width()) <= 1
                    && previewButton->height() == uiMetrics->property("largeActionHeight").toInt()
                    && sortButton->height() == previewButton->height()
                    && undoButton->mapToScene(QPointF()).y() >= sortButton->mapToScene(QPointF()).y() + sortButton->height(),
                    "large paired actions and separate undo");
                window->setX(previousX);
                QCoreApplication::processEvents();
                window->hide();
                checkLayout(!extension->isVisible(), "hide main");
                window->showNormal();
                checkLayout(extension->isVisible(), "restore main");
                window->setProperty("currentPage", 9);
                checkLayout(developer->property("visible").toBool(), "developer tools");
                window->setProperty("currentPage", 0);
                checkLayout(window->width() == 380 && !extension->isVisible() && !developer->property("visible").toBool(), "close extension");
                checkLayout(verifyExtensionInteraction(window, extension, uiMetrics, notifications), "page content, notification toggles and real close clicks");
                checkLayout(verifyHomeMenuAlignment(window, uiMetrics), "centered home menu text and left icons");
                checkLayout(verifyBusyPresentation(window, extension, backgroundTasks), "subwindow busy overlay and unlocked window movement");
            }
        }
        bool closeToTray = false;
        if (!engine.rootObjects().isEmpty()) {
            auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            if (mainWindow) {
                // 실제 시스템 트레이 없이도 QML 닫기와 화면 복원 연결을 검사한다.
                appLifecycle.setTrayAvailable(true);
                const bool intercepted = !mainWindow->close();
                closeToTray = intercepted && !mainWindow->isVisible() && characterWindow.shown();
                appLifecycle.openWindow();
                closeToTray = closeToTray && mainWindow->isVisible();
                appLifecycle.setTrayAvailable(false);
            }
        }
        const nookbit::JsonRuleSetRepository bundledRules(bundledTree, treeSchema);
        QObject* editorPage = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("ruleEditorPage"));
        // 규칙을 다시 불러오면 자동 전체 보기 대신 100% 배율로 돌아오는지 검사한다.
        bool defaultRuleZoom = false;
        if (editorPage) {
            editorPage->setProperty("zoom", 0.5);
            if (QMetaObject::invokeMethod(editorPage, "loadGraphFromDraft")) {
                QEventLoop reloadFrame;
                QTimer::singleShot(40, &reloadFrame, &QEventLoop::quit);
                reloadFrame.exec();
                defaultRuleZoom = qFuzzyCompare(editorPage->property("zoom").toDouble(), 1.0);
            }
        }
        QObject* ruleTestPanel = editorPage
            ? editorPage->findChild<QObject*>(QStringLiteral("ruleTestPanel")) : nullptr;
        QObject* watchSettingsDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("watchSettingsDialog"));
        QObject* historyDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("historyDialog"));
        QObject* recoveryDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("recoveryDialog"));
        QObject* recoveryConfirmDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("recoveryConfirmDialog"));
        QObject* characterSettingsDialog = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterSettingsDialog"));
        auto* desktopCharacter = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QQuickWindow*>(QStringLiteral("desktopCharacterWindow"));
        QObject* desktopSprite = desktopCharacter
            ? desktopCharacter->findChild<QObject*>(QStringLiteral("desktopCharacterSprite")) : nullptr;
        auto* characterBubble = desktopCharacter
            ? desktopCharacter->findChild<QQuickWindow*>(QStringLiteral("characterBubbleWindow")) : nullptr;
        // 인터넷 없이 두 제작 문서를 읽고 직접 저장 창구로 제공하는지 검사한다.
        auto* characterGuide = engine.rootObjects().isEmpty() ? nullptr
            : engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterGuideDownload"));
        const bool guideReady = !userGuides.text("rules").isEmpty() && !userGuides.text("character").isEmpty() &&
            characterGuide && characterGuide->property("guideKey").toString() == "character" &&
            characterGuide->findChild<QObject*>(QStringLiteral("guideSaveDialog"));
        // 프레임 선택과 임시 편집이 같은 재생 공간에서 동작하는지 검사한다.
        auto* workspace = characterSettingsDialog
            ? characterSettingsDialog->findChild<QObject*>(QStringLiteral("companionPreviewWorkspace")) : nullptr;
        auto* previewSprite = workspace
            ? workspace->findChild<QObject*>(QStringLiteral("companionPagePreview")) : nullptr;
        bool workspaceReady = workspace && previewSprite &&
            QMetaObject::invokeMethod(workspace, "selectFrame", Q_ARG(QVariant, QVariant(1))) &&
            !workspace->property("running").toBool() && previewSprite->property("sequenceIndex").toInt() == 1;
        if (workspaceReady) {
            auto* timeline = workspace->findChild<QObject*>(QStringLiteral("companionFrameTimeline"));
            auto* playbackStatus = workspace->findChild<QObject*>(QStringLiteral("companionPlaybackStatus"));
            const double duration = timeline ? timeline->property("durationMs").toDouble() : 0;
            workspaceReady = timeline && playbackStatus && duration > 0 &&
                playbackStatus->property("text").toString() == QStringLiteral("일시정지") &&
                QMetaObject::invokeMethod(timeline, "selectTime", Q_ARG(QVariant, QVariant(duration))) &&
                previewSprite->property("sequenceIndex").toInt() == 3 &&
                QMetaObject::invokeMethod(timeline, "selectTime", Q_ARG(QVariant, QVariant(0))) &&
                previewSprite->property("sequenceIndex").toInt() == 0;
            // 실제 페이지 배치가 갱신될 때까지 짧게 기다리는 함수.
            const auto settlePreview = [] {
                QEventLoop layout;
                QTimer::singleShot(60, &layout, &QEventLoop::quit);
                layout.exec();
            };
            QMetaObject::invokeMethod(engine.rootObjects().first(), "openCharacterSettings");
            workspace->setProperty("editing", false);
            settlePreview();
            auto* previewStage = workspace->findChild<QQuickItem*>(QStringLiteral("companionPreviewStage"));
            const double previewHeight = previewStage ? previewStage->height() : 0;
            workspace->setProperty("editing", true);
            settlePreview();
            workspaceReady = workspaceReady && previewStage && previewHeight > 0 && std::abs(previewStage->height() - previewHeight) < 1;
            auto* animationChoice = workspace->findChild<QObject*>(QStringLiteral("companionAnimationChoice"));
            auto* nextShortcut = workspace->findChild<QObject*>(QStringLiteral("companionNextFrameShortcut"));
            auto* world = workspace->findChild<QQuickItem*>(QStringLiteral("companionPreviewWorld"));
            auto* previewBubble = workspace->findChild<QObject*>(QStringLiteral("companionBubblePreview"));
            workspaceReady = workspaceReady && animationChoice && !animationChoice->property("enabled").toBool() &&
                nextShortcut && nextShortcut->property("enabled").toBool() && world && previewBubble &&
                !previewBubble->property("constrainToArea").toBool();
            if (workspaceReady) {
                const auto beforeZoom = world->mapFromItem(previewStage, QPointF(100, 120));
                workspaceReady = QMetaObject::invokeMethod(workspace, "zoomPreview", Q_ARG(QVariant, QVariant(0.5)),
                    Q_ARG(QVariant, QVariant(100)), Q_ARG(QVariant, QVariant(120))) &&
                    QLineF(beforeZoom, world->mapFromItem(previewStage, QPointF(100, 120))).length() < 0.01 &&
                    !workspace->property("dirty").toBool() && QMetaObject::invokeMethod(workspace, "resetPreview");
            }
            // 프레임 시작이 눈금 밖에 있어도 끝 시각에 붙고 임시 해제를 지키는지 검사한다.
            QVariant snapResult;
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(timeline, "snappedTime", Q_RETURN_ARG(QVariant, snapResult),
                Q_ARG(QVariant, QVariant(437)), Q_ARG(QVariant, QVariant(false))) && snapResult.toDouble() == 400 &&
                QMetaObject::invokeMethod(timeline, "resizedDuration", Q_RETURN_ARG(QVariant, snapResult),
                    Q_ARG(QVariant, QVariant(150)), Q_ARG(QVariant, QVariant(286)), Q_ARG(QVariant, QVariant(false))) && snapResult.toDouble() == 250 &&
                QMetaObject::invokeMethod(timeline, "resizedDuration", Q_RETURN_ARG(QVariant, snapResult),
                    Q_ARG(QVariant, QVariant(150)), Q_ARG(QVariant, QVariant(286)), Q_ARG(QVariant, QVariant(true))) && snapResult.toDouble() == 286;
            timeline->setProperty("snapEnabled", false);
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(timeline, "snappedTime", Q_RETURN_ARG(QVariant, snapResult),
                Q_ARG(QVariant, QVariant(437)), Q_ARG(QVariant, QVariant(false))) && snapResult.toDouble() == 437;
            timeline->setProperty("snapEnabled", true);
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "movePoint", Q_ARG(QVariant, QVariant(0)),
                Q_ARG(QVariant, QVariant(100)), Q_ARG(QVariant, QVariant(110)));
            const auto draft = workspace->property("draft").toMap().value("pivot").toMap();
            workspaceReady = workspaceReady && draft.value("x").toDouble() == 100 && draft.value("y").toDouble() == 110;
            auto* coordinates = workspace->findChild<QObject*>(QStringLiteral("companionPivotCoordinates"));
            workspaceReady = workspaceReady && coordinates && coordinates->property("text").toString().contains("X 100.0") &&
                coordinates->property("text").toString().contains("Y 110.0");
            // 숫자 입력과 프레임 전체 정렬이 소수점까지 동일한 점을 복사하는지 검사한다.
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "setCoordinates",
                Q_ARG(QVariant, QVariant(100.25)), Q_ARG(QVariant, QVariant(110.5)),
                Q_ARG(QVariant, QVariant(180.75)), Q_ARG(QVariant, QVariant(-50.125))) &&
                QMetaObject::invokeMethod(workspace, "alignFrames", Q_ARG(QVariant, QVariant(2)));
            const auto aligned = workspace->property("drafts").toMap();
            const auto originalSteps = workspace->property("authoredDescriptor").toMap().value("steps").toList();
            for (const auto& item : originalSteps) {
                const auto value = aligned.value(item.toMap().value("frameId").toString()).toMap();
                workspaceReady = workspaceReady && value.value("pivot").toMap().value("x").toDouble() == 100.25 &&
                    value.value("pivot").toMap().value("y").toDouble() == 110.5 &&
                    value.value("attachment").toMap().value("x").toDouble() == 80.5 &&
                    value.value("attachment").toMap().value("y").toDouble() == -160.625;
            }
            workspaceReady = workspaceReady && !originalSteps.isEmpty() && timeline->property("durationMs").toDouble() == duration;
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "fitPreview");
            if (workspaceReady) {
                auto* bubbleItem = qobject_cast<QQuickItem*>(previewBubble);
                const auto area = bubbleItem->mapRectToItem(previewStage, QRectF(0, 0, bubbleItem->width(), bubbleItem->height()));
                workspaceReady = area.left() >= -1 && area.top() >= -1 && area.right() <= previewStage->width() + 1 &&
                    area.bottom() <= previewStage->height() + 1 && QMetaObject::invokeMethod(workspace, "resetPreview");
            }
            // 지속시간 편집은 시간 커서를 보존하고 상한·하한 및 편집 모드를 지키는지 검사한다.
            auto* ruler = timeline ? timeline->findChild<QQuickItem*>(QStringLiteral("companionTimeRulerSeekArea")) : nullptr;
            workspaceReady = workspaceReady && ruler && ruler->height() == uiMetrics->property("companionTimelineRulerHeight").toDouble() &&
                QMetaObject::invokeMethod(workspace, "seekTime", Q_ARG(QVariant, QVariant(100))) &&
                QMetaObject::invokeMethod(workspace, "resizeFrame", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(600))) &&
                timeline->property("durationMs").toDouble() == duration + 400 &&
                timeline->property("currentTimeMs").toDouble() == 100 &&
                QMetaObject::invokeMethod(workspace, "resizeFrame", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(-500))) &&
                timeline->property("durationMs").toDouble() == duration - 184 &&
                QMetaObject::invokeMethod(workspace, "resetEdits") &&
                timeline->property("durationMs").toDouble() == duration;
            workspace->setProperty("editing", false);
            workspaceReady = workspaceReady && animationChoice->property("enabled").toBool();
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "resizeFrame",
                Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(600))) &&
                timeline->property("durationMs").toDouble() == duration;
            // 화면의 적용 신호가 저장 담당자로 이어지고 취소 시 마지막 적용값을 복원하는지 검사한다.
            workspace->setProperty("editing", true);
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "resizeFrame",
                Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(600))) && workspace->property("dirty").toBool();
            const QVariantMap edits{{"frames", workspace->property("drafts")}, {"durations", workspace->property("durationDrafts")}};
            workspaceReady = workspaceReady && QMetaObject::invokeMethod(workspace, "applyRequested", Q_ARG(QVariant, QVariant(edits))) &&
                !companionEdits.applied().isEmpty() && !workspace->property("dirty").toBool();
            settlePreview();
            auto* savedNotice = workspace->findChild<QQuickItem*>(QStringLiteral("companionSavedNotice"));
            workspaceReady = workspaceReady && savedNotice && savedNotice->opacity() > 0 && companionEdits.status().isEmpty() &&
                std::abs(previewStage->height() - previewHeight) < 1 &&
                QMetaObject::invokeMethod(workspace, "resizeFrame", Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(700))) &&
                workspace->property("dirty").toBool() && QMetaObject::invokeMethod(workspace, "reloadEdits") &&
                !workspace->property("dirty").toBool() && timeline->property("durationMs").toDouble() == duration + 400;
            workspaceReady = workspaceReady && companionEdits.applyChanges({}) && !workspace->property("dirty").toBool() &&
                timeline->property("durationMs").toDouble() == duration;
            workspace->setProperty("editing", false);
        }
        const bool buttonFeedbackReady = verifyButtonFeedback(engine, uiMetrics);
        const bool gesturesReady = verifyCompanionGestures(desktopCharacter, characterWindow, companionInteractions, characterController);
        const bool ready = gesturesReady && buttonFeedbackReady && workspaceReady && guideReady && defaultRuleZoom && compactLayout && closeToTray && !engine.rootObjects().isEmpty() &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("compactHome")) != nullptr &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("extensionPanel")) != nullptr &&
                           characterSettingsDialog != nullptr &&
                           engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterAnchorEditor")) != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterAnchorEditor")), "open") &&
                           QMetaObject::invokeMethod(engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("characterAnchorEditor")), "close") &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openCharacterSettings") &&
                           QMetaObject::invokeMethod(characterSettingsDialog, "open") &&
                           characterSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(characterSettingsDialog, "close") &&
                           characterSettings.selectedKey() == QStringLiteral("builtin") &&
                           desktopCharacter != nullptr && desktopSprite != nullptr &&
                           verifyCharacterAnchorStability(desktopCharacter, characterSettings, characterWindow) &&
                           characterBubble != nullptr && characterBubble->color().alpha() == 0 &&
                           QMetaObject::invokeMethod(desktopCharacter, "testBubble") &&
                           desktopCharacter->property("testingBubble").toBool() &&
                           desktopCharacter->color().alpha() == 0 &&
                           desktopCharacter->flags().testFlag(Qt::FramelessWindowHint) &&
                           desktopCharacter->flags().testFlag(Qt::WindowStaysOnTopHint) &&
                           desktopCharacter->transientParent() == nullptr &&
                           !desktopCharacter->close() && !characterWindow.shown() &&
                           editorPage != nullptr && ruleTestPanel != nullptr &&
                           QMetaObject::invokeMethod(ruleTestPanel, "open") &&
                           ruleTestPanel->property("opened").toBool() &&
                           QMetaObject::invokeMethod(ruleTestPanel, "close") &&
                           watchSettingsDialog != nullptr && historyDialog != nullptr && recoveryDialog != nullptr && recoveryConfirmDialog != nullptr &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openWatchSettings") &&
                           watchSettingsDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(watchSettingsDialog, "close") &&
                           QMetaObject::invokeMethod(engine.rootObjects().first(), "openHistory") &&
                           historyDialog->property("opened").toBool() &&
                           QMetaObject::invokeMethod(historyDialog, "close") &&
                           QMetaObject::invokeMethod(recoveryDialog, "open") && recoveryDialog->property("visible").toBool() &&
                           QMetaObject::invokeMethod(recoveryDialog, "close") &&
                           QMetaObject::invokeMethod(recoveryConfirmDialog, "open") && recoveryConfirmDialog->property("visible").toBool() &&
                           QMetaObject::invokeMethod(recoveryConfirmDialog, "close") &&
                           pendingFiles.count() == 0 && !previewController.hasPreview() &&
                           bundledRules.load().hasValue() &&
                           QFile::exists(QStringLiteral(":/nookbit/001_initial.sql")) &&
                           QFile::exists(QStringLiteral(":/nookbit/imagepack.schema.json")) &&
                           QFile::exists(QStringLiteral(":/nookbit/watch.schema.json"));
        if (!ready) {
            QTextStream(stderr) << "UI verification: buttons=" << buttonFeedbackReady << " companion=" << workspaceReady
                << " layout=" << compactLayout << " guide=" << guideReady << " zoom=" << defaultRuleZoom << " tray=" << closeToTray << Qt::endl;
            return EXIT_FAILURE;
        }
        // 비동기로 읽는 내장 PNG의 크기 검증까지 마친 뒤 자동 검사를 완료한다.
        characterWindow.setShown(true);
        auto* sprite = desktopSprite;
        QTimer spriteCheck;
        spriteCheck.setInterval(10);
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &application, [] { QCoreApplication::exit(EXIT_FAILURE); });
        QObject::connect(&spriteCheck, &QTimer::timeout, &application, [sprite] {
            if (sprite->property("ready").toBool()) QCoreApplication::exit(EXIT_SUCCESS);
            else if (!sprite->property("error").toString().isEmpty()) {
                QTextStream(stderr) << sprite->property("error").toString() << Qt::endl;
                QCoreApplication::exit(EXIT_FAILURE);
            }
        });
        spriteCheck.start();
        deadline.start(2000);
        return application.exec();
    }

    return application.exec();
}

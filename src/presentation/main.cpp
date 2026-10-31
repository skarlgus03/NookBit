#include "presentation/pending_files_model.hpp"
#include "app/feature_connections.hpp"
#include "app/feature_runtime_host.hpp"
#include "app/feature_preferences.hpp"
#include "presentation/feature_settings_controller.hpp"
#include "app/notification_runtime.hpp"
#include "app/folder_watch_runtime.hpp"
#include "app/file_organizer_runtime.hpp"
#include "app/companion_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include "app/companion_tray_action.hpp"
#include "presentation/file_icon_provider.hpp"
#include "presentation/file_preview_provider.hpp"
#include "presentation/preview_controller.hpp"
#include "presentation/history_controller.hpp"
#include "presentation/recovery_controller.hpp"
#include "presentation/watch_controller.hpp"
#include "presentation/rule_editor_controller.hpp"
#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QApplication>
#include <QQuickStyle>
#include <QPalette>
#include "presentation/ui_settings_controller.hpp"
#include "presentation/ui_font.hpp"
#include "presentation/tutorial_controller.hpp"
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
#include "presentation/companion_edit_controller.hpp"
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <algorithm>
#include <cmath>
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

// 리피터·로더의 화면 자식도 객체 소유 트리와 무관하게 찾는 자동 검사용 함수.
static QQuickItem* findVisualItem(QQuickItem* item, const QString& name) {
    if (!item) return nullptr;
    if (item->objectName() == name) return item;
    for (auto* child : item->childItems())
        if (auto* found = findVisualItem(child, name)) return found;
    return nullptr;
}

// 등록 로더·보조 창의 객체 소유 트리와 화면 트리를 함께 찾는 자동 검사용 함수.
template<class T>
static T findUiChild(QObject* root, const QString& name) {
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (!object || visited.contains(object)) continue;
        visited.insert(object);
        if (object->objectName() == name)
            if (auto found = qobject_cast<T>(object)) return found;
        for (auto* child : object->children()) pending.append(child);
        if (auto* item = qobject_cast<QQuickItem*>(object))
            for (auto* child : item->childItems()) pending.append(child);
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
    }
    return nullptr;
}

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

// 공통 버튼의 페이드·실제 누름·선택·비활성을 밝고 어두운 테마에서 검사하는 함수.
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
    for (const auto& type : {QStringLiteral("AppButton"), QStringLiteral("LargeActionButton"), QStringLiteral("HomeMenuButton"),
            QStringLiteral("NavigationButton"), QStringLiteral("RuleEditorButton")}) {
        QQmlComponent component(&engine);
        component.setData(QStringLiteral("import QtQuick\nimport NookBit\n%1 { text: 'Feedback'; x: 40; y: 20; width: 220; height: 64; checkable: true }")
            .arg(type).toUtf8(), QUrl(QStringLiteral("qrc:/button-feedback-fixture.qml")));
        std::unique_ptr<QObject> object(component.create());
        auto* button = qobject_cast<QQuickItem*>(object.get());
        if (!button) { QTextStream(stderr) << component.errorString(); return false; }
        button->setParentItem(host.contentItem());
        if (type == "LargeActionButton") button->setProperty("primary", true);
        auto* surface = button->findChild<QQuickItem*>(QStringLiteral("buttonFeedbackSurface"));
        auto* layer = button->findChild<QQuickItem*>(QStringLiteral("buttonFeedbackLayer"));
        if (!surface || !layer) return false;
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
            if (layer->opacity() > 0.001) return fail("idle layer");
            const QRectF normalGeometry(surface->position(), surface->size());
            // 홈 선택은 호버 여부와 무관하게 유지하고 포인터 조작에는 테두리를 만들지 않는다.
            if (type == "HomeMenuButton") {
                if (surface->property("color").value<QColor>().alpha() != 0
                    || QQmlProperty(surface, QStringLiteral("border.width")).read().toInt() != 0)
                    return fail("borderless home item");
                button->setProperty("selected", true);
                settle(150);
                const auto selected = layer->property("color").value<QColor>();
                pointer(QEvent::MouseMove, QPointF(100, 45), Qt::NoButton, Qt::NoButton);
                settle(150);
                if (layer->opacity() < 0.999 || layer->property("color").value<QColor>() != selected)
                    return fail("persistent home selection");
                button->setProperty("selected", false);
                pointer(QEvent::MouseMove, QPointF(5, 5), Qt::NoButton, Qt::NoButton);
                settle(150);
            }
            const auto hoverColor = layer->property("color").value<QColor>();
            pointer(QEvent::MouseMove, QPointF(100, 45), Qt::NoButton, Qt::NoButton);
            double previousOpacity = 0;
            // 호버 전 구간에서 검은 RGB 중간색이나 눌림 상태가 끼어들지 않는지 검사한다.
            for (int frame = 0; frame < 7; ++frame) {
                settle(20);
                const auto currentColor = layer->property("color").value<QColor>();
                if (!button->property("hovered").toBool() || surface->property("pushed").toBool()
                    || currentColor != hoverColor || currentColor.alpha() != 255
                    || layer->opacity() + 0.001 < previousOpacity || surface->scale() != 1)
                    return fail("stable hover fade");
                previousOpacity = layer->opacity();
            }
            if (layer->opacity() < 0.999) return fail("hover completion");
            pointer(QEvent::MouseButtonPress, QPointF(100, 45), Qt::LeftButton, Qt::LeftButton);
            settle(20);
            if (!surface->property("pushed").toBool() || layer->property("color").value<QColor>() == hoverColor
                || surface->scale() != 1 || QRectF(surface->position(), surface->size()) != normalGeometry
                || button->width() != 220 || button->height() != 64) return fail("stable press");
            if (type == "HomeMenuButton" && QQmlProperty(surface, QStringLiteral("border.width")).read().toInt() != 0)
                return fail("no home click outline");
            pointer(QEvent::MouseButtonRelease, QPointF(100, 45), Qt::LeftButton, Qt::NoButton);
            button->setProperty("checked", true);
            settle(150);
            auto* text = type == "HomeMenuButton" ? button->findChild<QObject*>(QStringLiteral("homeMenuLabel"))
                : button->property("contentItem").value<QQuickItem*>();
            if (!text || text->property("color").value<QColor>() != button->property("buttonForeground").value<QColor>())
                return fail("selected text");
            pointer(QEvent::MouseButtonPress, QPointF(100, 45), Qt::LeftButton, Qt::LeftButton);
            if (!surface->property("pushed").toBool()) return fail("real pointer press");
            pointer(QEvent::MouseButtonRelease, QPointF(100, 45), Qt::LeftButton, Qt::NoButton);
            if (surface->property("pushed").toBool()) return fail("no synthetic click pulse");
            button->setProperty("enabled", false);
            settle(metrics->property("buttonReleaseMs").toInt() + 30);
            if (surface->property("pushed").toBool() || surface->opacity() >= 1 || layer->opacity() > 0.001)
                return fail("disabled");
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
    auto* area = findUiChild<QQuickItem*>(window, QStringLiteral("companionGestureInput"));
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
    auto* button = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("extensionCloseButton"));
    auto* rules = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("ruleEditorPage"));
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
    for (const int page : {1, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 14}) {
        mainWindow->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        if (page == 3 || page == 8) {
            QEventLoop navigationFrame;
            QTimer::singleShot(metrics->property("pageSlideDuration").toInt() + 40, &navigationFrame, &QEventLoop::quit);
            navigationFrame.exec();
        }
        auto* home = findUiChild<QObject*>(mainWindow, QStringLiteral("compactHome"));
        auto* title = findUiChild<QObject*>(mainWindow, QStringLiteral("extensionPageTitle"));
        const auto titleFont = title ? title->property("font").value<QFont>() : QFont();
        const auto sharedFont = metrics->property("applicationFont").value<QFont>();
        if (!title || titleFont.families() != sharedFont.families() || !titleFont.styleName().isEmpty() || titleFont.weight() != QFont::Bold) return false;
        const int expectedMenu = (page == 3 || page == 6 || page == 8 || page == 9 || page == 10 || page == 12) ? 8
            : page == 11 ? 14 : page;
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
            // 긴 버튼 이름을 넣어도 빈 공간 대신 글자가 먼저 잘리지 않는지 검사한다.
            const auto originalTestText = ruleTestButton->property("text");
            ruleTestButton->setProperty("text", QStringLiteral("파일 분류 규칙을 테스트하고 결과를 확인합니다"));
            QCoreApplication::processEvents();
            auto* testText = ruleTestButton->property("contentItem").value<QQuickItem*>();
            const bool fullButtonText = testText && !testText->property("truncated").toBool() &&
                testText->width() + 1 >= testText->implicitWidth();
            ruleTestButton->setProperty("text", originalTestText);
            QCoreApplication::processEvents();
            if (!fullButtonText) return false;
            // 규칙 문서 버튼이 읽기 창 대신 직접 저장 창구에 연결되는지 검사한다.
            auto* guideButton = rules->findChild<QQuickItem*>(QStringLiteral("rulesUserGuideButton"));
            auto* guide = rules->findChild<QObject*>(QStringLiteral("rulesGuideDownload"));
            if (!guideButton || !guide || guide->property("guideKey").toString() != "rules" ||
                !guide->findChild<QObject*>(QStringLiteral("guideSaveDialog"))) return false;
            // 역할별 메뉴는 실제 버튼으로 열고 같은 버튼으로 다시 닫을 수 있어야 한다.
            for (const auto& names : {std::pair{"ruleFileMenuButton", "ruleFileMenu"},
                                      std::pair{"ruleEditMenuButton", "ruleEditMenu"},
                                      std::pair{"ruleViewMenuButton", "ruleViewMenu"},
                                      std::pair{"ruleHelpButton", "ruleHelpPanel"}}) {
                auto* menuButton = rules->findChild<QQuickItem*>(QString::fromLatin1(names.first));
                auto* menu = rules->findChild<QObject*>(QString::fromLatin1(names.second));
                if (!menuButton || !menu) return false;
                click(menuButton);
                if (!menu->property("opened").toBool()) {
                    QTextStream(stderr) << "Rule toolbar menu did not open: " << names.second << Qt::endl;
                    return false;
                }
                if (QString::fromLatin1(names.second) == "ruleHelpPanel" && !guideButton->isVisible()) return false;
                click(menuButton);
                if (menu->property("visible").toBool()) {
                    QTextStream(stderr) << "Rule toolbar menu did not toggle closed: " << names.second << Qt::endl;
                    return false;
                }
            }
            // 추가 메뉴가 조건·분기·종착점을 중복 없이 나누고 제한된 크기로 열리는지 검사한다.
            const auto groups = rules->property("nodeAddGroups").value<QJSValue>().toVariant().toList();
            if (groups.size() != 3) return false;
            const QStringList expectedTypes{"itemType", "extension", "size", "name", "age", "otherwise", "destination", "exclude"};
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
            auto* list = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("historyList"));
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
            auto* themeSwitch = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("globalDarkModeSwitch"));
            auto* lightButton = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("globalLightModeButton"));
            const bool initial = mainWindow->property("editorDark").toBool();
            if (!themeSwitch || !lightButton) return false;
            click(initial ? lightButton : themeSwitch);
            if (mainWindow->property("editorDark").toBool() == initial || rules->property("darkMode").toBool() == initial) return false;
            // 네이티브 메뉴와 QML 창이 같은 테마 색상표를 사용하는지 검사한다.
            const auto themeColors = mainWindow->property("theme").value<QJSValue>().toVariant().toMap();
            if (QGuiApplication::palette().color(QPalette::Window) != QColor(themeColors.value("window").toString()) ||
                QGuiApplication::palette().color(QPalette::Text) != QColor(themeColors.value("text").toString())) return false;
            click(initial ? themeSwitch : lightButton);
            if (mainWindow->property("editorDark").toBool() != initial) return false;
        }
        if (page == 3) {
            // 감시 조건 네 항목에 실제 키 입력 후 Enter로 숫자를 확정하는 검사.
            for (const auto* name : {"watchIntervalSpin", "watchCountSpin", "watchSizeSpin", "watchAgeSpin"}) {
                auto* spin = findUiChild<QQuickItem*>(mainWindow, QString::fromLatin1(name));
                if (QString::fromLatin1(name) == QStringLiteral("watchIntervalSpin")) {
                    auto* custom = findUiChild<QObject*>(mainWindow, QStringLiteral("watchIntervalCustomButton"));
                    if (!custom || !QMetaObject::invokeMethod(custom, "clicked")) return false;
                    QCoreApplication::processEvents();
                }
                if (!spin || !spin->property("editable").toBool()
                    || spin->height() != metrics->property("controlHeight").toInt()) return false;
                // 카드 아래의 입력도 실제로 보이는 위치까지 스크롤한 뒤 조작하는 검사.
                auto* scroll = findUiChild<QQuickItem*>(mainWindow, QStringLiteral("watchSettingsScroll"));
                auto* viewport = scroll ? scroll->property("contentItem").value<QQuickItem*>() : nullptr;
                if (!viewport) return false;
                const auto contentY = viewport->property("contentY").toDouble();
                const auto fieldY = spin->mapToItem(viewport, QPointF()).y() + contentY;
                viewport->setProperty("contentY", std::clamp(fieldY - metrics->property("spaceGroup").toInt(), 0.0,
                    std::max(0.0, viewport->property("contentHeight").toDouble() - viewport->height())));
                QCoreApplication::processEvents();
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
                if (spin->property("value").toInt() != 38) {
                    QTextStream(stderr) << "Watch increase blocked: " << name << " fieldY=" << spin->mapToItem(viewport, QPointF()).y()
                        << " viewport=" << viewport->height() << " enabled=" << spin->isEnabled() << Qt::endl;
                    return false;
                }
                click(decrease);
                if (spin->property("value").toInt() != 37) return false;
                spin->setEnabled(wasEnabled);
            }
            auto* sizeUnit = findUiChild<QObject*>(mainWindow, QStringLiteral("watchSizeUnitCombo"));
            if (!sizeUnit || sizeUnit->property("count").toInt() != 4) return false;
        }
        if (page == 8) {
            // 버전 정보가 추가된 네 설정 그룹이 같은 규격으로 배치되는지 검사한다.
            auto* settings = findUiChild<QObject*>(mainWindow, QStringLiteral("settingsPage"));
            if (!settings) return false;
            const auto titles = settings->findChildren<QQuickItem*>(QStringLiteral("settingsSectionTitle"));
            const auto cards = settings->findChildren<QQuickItem*>(QStringLiteral("settingsSectionCard"));
            if (titles.size() != 5 || cards.size() != 5) return false;
            auto* version = findUiChild<QObject*>(settings, QStringLiteral("applicationVersionLabel"));
            if (!version || version->property("text").toString() != "NookBit " + QCoreApplication::applicationVersion()) return false;
            for (auto* title : titles) {
                if (title->property("font").value<QFont>().pixelSize() != metrics->property("textSettingsGroup").toInt() ||
                    !title->isVisible() || title->height() <= 0) return false;
            }
            for (auto* card : cards) {
                if (!card->isVisible() || card->height() <= 0 || card->width() <= 0) return false;
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

// 홈 메뉴의 아이콘과 글씨가 공통 간격으로 왼쪽 정렬되는지 검사하는 함수.
static bool verifyHomeMenuAlignment(QQuickWindow* root, QObject* metrics) {
    auto* home = root->findChild<QQuickItem*>(QStringLiteral("compactHome"));
    if (!home) return false;
    // Repeater의 대리 항목을 포함한 실제 화면 트리에서 이름으로 찾는 함수.
    const auto find = [](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems()) if (auto* found = self(self, child, name)) return found;
        return nullptr;
    };
    for (const int page : {7, 1, 4, 5, 8, 14}) {
        auto* button = find(find, home, QStringLiteral("homeNavigation%1").arg(page));
        auto* label = button ? find(find, button, QStringLiteral("homeMenuLabel")) : nullptr;
        auto* icon = button ? find(find, button, QStringLiteral("homeMenuIcon")) : nullptr;
        if (!button || !label || !icon) return false;
        const auto labelOrigin = label->mapToItem(button, QPointF());
        const auto iconOrigin = icon->mapToItem(button, QPointF());
        if (label->property("horizontalAlignment").toInt() != Qt::AlignLeft
            || std::abs(labelOrigin.x() - iconOrigin.x() - icon->width() - metrics->property("spaceGroup").toInt()) > 1
            || std::abs(labelOrigin.y() + label->height() / 2 - button->height() / 2) > 1
            || std::abs(iconOrigin.x() - metrics->property("controlPadding").toInt()) > 1
            || icon->width() != metrics->property("mainMenuIconSize").toInt()
            || button->height() != metrics->property("mainNavigationHeight").toInt()) return false;
    }
    return true;
}

// 무파일 작업으로 검사·되돌리기 표시가 서브창에 속하고 제목 이동은 잠기지 않는지 검사하는 함수.
static bool verifyBusyPresentation(QQuickWindow* root, QQuickWindow* extension, nookbit::BackgroundTaskRunner& runner) {
    auto* popup = findUiChild<QObject*>(root, QStringLiteral("operationProgressPopup"));
    auto* veil = findUiChild<QQuickItem*>(root, QStringLiteral("operationProgressVeil"));
    auto* recoveryIndicator = findUiChild<QQuickItem*>(root, QStringLiteral("recoveryInspectionIndicator"));
    auto* drag = findUiChild<QQuickItem*>(root, QStringLiteral("homeWindowDragArea"));
    auto* menu = findUiChild<QQuickItem*>(root, QStringLiteral("homeNavigationMenu"));
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
        // 복구 페이지가 자체 시작한 읽기 검사를 끝낸 뒤 표시 전용 대기 작업을 넣는다.
        if (runner.busy()) {
            QEventLoop startup;
            QObject::connect(&runner, &nookbit::BackgroundTaskRunner::busyChanged, &startup, [&] {
                if (!runner.busy()) startup.quit();
            });
            QTimer::singleShot(5000, &startup, &QEventLoop::quit);
            startup.exec();
            if (runner.busy()) return false;
        }
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
    nookbit::configureUiTextRendering();
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    QCoreApplication::setOrganizationName(QStringLiteral("NookBitProject"));
    QCoreApplication::setApplicationName(QStringLiteral("NookBit"));
    QCoreApplication::setApplicationVersion(QStringLiteral(NOOKBIT_VERSION_STRING));
    const auto arguments = QCoreApplication::arguments();
    if (arguments.contains(QStringLiteral("--version"))) {
        QTextStream(stdout) << "NookBit " << QCoreApplication::applicationVersion() << Qt::endl;
        return EXIT_SUCCESS;
    }
    const bool verifyFeaturePreferences = arguments.contains(QStringLiteral("--verify-feature-preferences"));
    const bool verifyTutorialSkip = arguments.contains(QStringLiteral("--verify-tutorial-skip"));
    const bool verifyTutorial = verifyTutorialSkip || arguments.contains(QStringLiteral("--verify-tutorial"));
    const bool verifyFeatures = verifyTutorial || verifyFeaturePreferences || arguments.contains(QStringLiteral("--verify-feature-startup"));
    const bool verifyUi = verifyFeatures || arguments.contains(QStringLiteral("--verify-ui"));
    const auto selection = nookbit::parseFeatureSelection(arguments);
    if (!selection) { QTextStream(stderr) << QString::fromStdString(selection.error().message) << Qt::endl; return EXIT_FAILURE; }
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

    std::unique_ptr<QTemporaryDir> isolatedFeatureData;
    if (verifyFeatures) {
        isolatedFeatureData = std::make_unique<QTemporaryDir>();
        if (!isolatedFeatureData->isValid()) return EXIT_FAILURE;
    }
    const auto appData = verifyFeatures ? isolatedFeatureData->path() : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const auto fallbackDestination = QDir(documents).filePath(QStringLiteral("NookBit Organized"));
    auto preferenceStorage = verifyFeatures
        ? std::make_unique<QSettings>(QDir(appData).filePath("preferences.ini"), QSettings::IniFormat)
        : std::make_unique<QSettings>();
    auto& uiPreferences = *preferenceStorage;
    nookbit::FeaturePreferences featurePreferences(uiPreferences);
    // 저장 복원 검사에서는 임시 INI에만 선택을 먼저 기록한 뒤 실제 시작 읽기 경로를 사용한다.
    if (verifyFeaturePreferences && !featurePreferences.save(selection.value())) return EXIT_FAILURE;
    // 자동 검사는 개인 모듈 설정을 읽거나 쓰지 않고 기존 전체 기능 기준을 유지한다.
    const auto loadedFeatures = verifyUi && !verifyFeaturePreferences
        ? nookbit::Result<nookbit::FeatureSelection>::success({}) : featurePreferences.load();
    const auto savedFeatures = loadedFeatures ? loadedFeatures.value() : nookbit::FeatureSelection{false, false, false, false, false};
    const auto selectedFeatures = nookbit::parseFeatureSelection(verifyFeaturePreferences ? QStringList{} : arguments, savedFeatures);
    auto startup = nookbit::resolveFeatureStartup(selectedFeatures.value());
    const auto featureLoadError = loadedFeatures ? QString() : QString::fromStdString(loadedFeatures.error().message);
    if (!featureLoadError.isEmpty()) startup.messages.prepend(featureLoadError);
    if (verifyUi && !verifyFeatures && startup.active != nookbit::FeatureSelection{}) {
        QTextStream(stderr) << "Use --verify-feature-startup for disabled feature combinations." << Qt::endl;
        return EXIT_FAILURE;
    }
    const bool featureOverride = !verifyFeaturePreferences && std::any_of(arguments.cbegin(), arguments.cend(),
        [](const QString& argument) { return argument.startsWith("--disable-feature="); });
    nookbit::FeatureSettingsController featureSettings(savedFeatures, startup.active, featureOverride,
        [&featurePreferences, verifyUi, verifyFeaturePreferences](nookbit::FeatureSelection value) {
            // 화면 자동 검사에서는 저장 콜백만 성공시키고 사용자 환경은 변경하지 않는 함수.
            return verifyUi && !verifyFeaturePreferences ? nookbit::Result<void>::success() : featurePreferences.save(value);
        }, featureLoadError);
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
    nookbit::UiPageRegistry uiPages({2, 8, 9, 12, 13, 14});
    const auto watchFolder = verifyFeatures ? QDir(appData).filePath("watch")
        : QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (verifyFeatures && startup.active.watch) QDir().mkpath(watchFolder);
    nookbit::FeatureRuntimeHost features(startup, {appData, defaultDestination,
        QCoreApplication::applicationDirPath(), watchFolder, verifyUi}, uiPreferences, application, uiPages);
    if (!features.startupResult()) {
        QTextStream(stderr) << QString::fromStdString(features.startupResult().error().message) << Qt::endl;
        return EXIT_FAILURE;
    }
    auto* organizer = features.organizer();
    auto* folderWatch = features.watch();
    auto* notificationRuntime = features.notifications();
    auto* companion = features.companion();
    nookbit::TutorialController tutorial(uiPreferences, !verifyUi || verifyTutorial);
    const auto tutorialText = [](const char* text) {
        // 기본 기능과 분리된 공통 사용 안내를 번역하는 함수.
        return QCoreApplication::translate("Tutorial", text);
    };
    const auto hostTutorial = uiPages.addTutorialSteps({
        {"host.welcome", "host", tutorialText("NookBit에 오신 걸 환영해요"),
            tutorialText("파일 정리부터 컴패니언까지, 사용법을 살펴봐요."),
            {tutorialText("메인 메뉴에서 기능을 선택하면 옆에 페이지가 열려요."),
             tutorialText("다음을 누르거나 아래 목차에서 궁금한 항목을 골라 보세요.")}, {}, 0},
        {"host.finish", "host", tutorialText("이제 직접 사용해 보세요"),
            tutorialText("작은 테스트 파일부터 시작해 보세요."),
            {tutorialText("파일 정리에 파일을 넣고 결과 미리보기를 확인해 보세요."),
             tutorialText("필요한 기능은 모듈 관리에서 선택할 수 있어요. 저장 후 앱을 완전히 종료했다 다시 켜면 적용돼요.")},
            tutorialText("이 안내는 설정 → 정보 → 튜토리얼 다시 보기에서 다시 열 수 있어요."), 1000}
    }, tutorial);
    if (!hostTutorial) {
        QTextStream(stderr) << QString::fromStdString(hostTutorial.error().message) << Qt::endl;
        return EXIT_FAILURE;
    }
    bool featureBindingsClean = true;
    QQmlApplicationEngine engine;
    // 테마와 컨트롤을 만들기 전에 공통 글꼴을 확정하고 창에서도 명시적으로 공유한다.
    auto* uiMetrics = engine.singletonInstance<QObject*>("NookBit", "UiMetrics");
    if (!uiMetrics || uiMetrics->property("textBody").toInt() <= 0) {
        QTextStream(stderr) << "Cannot load shared UI metrics." << Qt::endl;
        return EXIT_FAILURE;
    }
    if (!nookbit::loadApplicationUiFonts(*uiMetrics)) {
        QTextStream(stderr) << "Cannot load bundled UI fonts." << Qt::endl;
        return EXIT_FAILURE;
    }
    const auto baseFont = nookbit::applicationUiFont(*uiMetrics);
    application.setFont(baseFont);
    uiMetrics->setProperty("applicationFont", baseFont);
    if (verifyUi) QTextStream(stdout) << "UI font: " << baseFont.families().join(", ")
        << " weight=" << baseFont.weight() << " style=" << baseFont.styleName() << Qt::endl;
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
    if (organizer) {
        engine.addImageProvider(QStringLiteral("fileicons"), new nookbit::FileIconProvider);
        engine.addImageProvider(QStringLiteral("filepreviews"), new nookbit::FilePreviewProvider);
    }
    // 실제 기능 창구만 빌려주고 꺼진 기능의 대체 컨트롤러나 실행기는 만들지 않는다.
    const auto expose = [&engine](const QString& name, QObject* value) { engine.rootContext()->setContextProperty(name, value); };
    expose("pendingFiles", organizer ? &organizer->pendingFiles() : nullptr);
    expose("backgroundTasks", organizer ? &organizer->tasks() : nullptr);
    expose("previewController", organizer ? &organizer->preview() : nullptr);
    expose("recoveryController", organizer ? &organizer->recovery() : nullptr);
    expose("historyController", organizer ? &organizer->history() : nullptr);
    expose("ruleEditor", organizer ? &organizer->rules() : nullptr);
    expose("watchController", folderWatch ? &folderWatch->controller() : nullptr);
    expose("notifications", notificationRuntime ? &notificationRuntime->controller() : nullptr);
    expose("appLifecycle", &appLifecycle);
    expose("uiSettings", &uiSettings);
    expose("tutorial", &tutorial);
    expose("featureSettings", &featureSettings);
    expose("userGuides", &userGuides);
    expose("uiPages", &uiPages);
    engine.rootContext()->setContextProperty("featureStartupMessages", startup.messages);
    engine.rootContext()->setContextProperty("organizerSettings", organizer ? organizer->settingsUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("watchSettingsEntry", folderWatch ? folderWatch->settingsEntryUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("watchProposalUi", folderWatch ? folderWatch->proposalUi() : QVariantMap{});
    engine.rootContext()->setContextProperty("companionDesktop", companion ? companion->desktopUi() : QVariantMap{});
    if (verifyFeatures) QObject::connect(&engine, &QQmlEngine::warnings, &engine, [&featureBindingsClean](const QList<QQmlError>& errors) {
        // 일부 기능이 없는 상태에서 숨은 필수 참조나 잘못된 바인딩을 놓치지 않는 함수.
        featureBindingsClean = false;
        for (const auto& error : errors) QTextStream(stderr) << error.toString() << Qt::endl;
    });
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &application,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("NookBit"), QStringLiteral("Main"));

    // 호스트는 공통 트레이 동작을 만들고 컴패니언의 메뉴 기여 수명만 보관한다.
    QMenu trayMenu;
    auto* openAction = trayMenu.addAction(QStringLiteral("NookBit 열기"));
    auto companionTray = companion ? companion->createTrayAction(trayMenu) : std::unique_ptr<nookbit::CompanionTrayAction>{};
    trayMenu.addSeparator();
    auto* exitAction = trayMenu.addAction(QStringLiteral("완전 종료"));
    QSystemTrayIcon tray(application.windowIcon());
    tray.setToolTip(QStringLiteral("NookBit · 창을 닫아도 실행 중"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(openAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::openWindow);
    QObject::connect(exitAction, &QAction::triggered, &appLifecycle, &nookbit::AppLifecycleController::requestExit);
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
    const auto refreshExitState = [&appLifecycle, &features, exitAction] {
        const bool busy = features.busy();
        appLifecycle.setBusy(busy);
        exitAction->setEnabled(!busy);
        exitAction->setText(busy ? QStringLiteral("작업 중 · 완료 후 종료 가능") : QStringLiteral("완전 종료"));
    };
    QObject::connect(&features, &nookbit::FeatureRuntimeHost::busyChanged, &appLifecycle, refreshExitState);
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

    // 실제 안내 버튼의 탐색·완료·다시 보기가 기능 설정을 변경하지 않는지 검사한다.
    if (verifyTutorial) {
        QEventLoop settle;
        QTimer::singleShot(160, &settle, &QEventLoop::quit);
        settle.exec();
        auto* window = engine.rootObjects().isEmpty() ? nullptr : engine.rootObjects().first();
        auto* page = window ? findUiChild<QObject*>(window, "tutorialPage") : nullptr;
        auto* next = window ? findUiChild<QObject*>(window, "tutorialNextButton") : nullptr;
        auto* previous = window ? findUiChild<QObject*>(window, "tutorialPreviousButton") : nullptr;
        auto* skip = window ? findUiChild<QObject*>(window, "tutorialSkipButton") : nullptr;
        auto* contents = window ? findUiChild<QObject*>(window, "tutorialContents") : nullptr;
        auto* details = window ? findUiChild<QObject*>(window, "tutorialDetailsButton") : nullptr;
        bool valid = window && page && next && previous && skip && contents && details && tutorial.required()
            && window->property("currentPage").toInt() == 13 && featureBindingsClean && organizer && companion;
        const auto beforeFolder = organizer ? organizer->preview().defaultDestinationRoot() : QString();
        const auto beforeRules = organizer ? organizer->preview().activeRuleLabel() : QString();
        const auto beforeShown = companion && companion->window().shown();
        const auto beforeCount = organizer ? organizer->pendingFiles().count() : 0;
        if (valid) {
            const auto count = page->property("stepCount").toInt();
            valid = count == uiPages.tutorialSteps().size() && count >= 7 && page->property("stepIndex").toInt() == 0;
            valid = QMetaObject::invokeMethod(next, "clicked") && valid;
            valid = valid && page->property("stepIndex").toInt() == 1;
            valid = valid && !page->property("detailsExpanded").toBool() && details->property("visible").toBool();
            valid = QMetaObject::invokeMethod(details, "clicked") && valid;
            valid = valid && page->property("detailsExpanded").toBool();
            valid = QMetaObject::invokeMethod(previous, "clicked") && valid;
            valid = valid && page->property("stepIndex").toInt() == 0 && !page->property("detailsExpanded").toBool();
            // 마지막이 아닌 단계에서 목차를 선택해도 안내만 바뀌는지 검사한다.
            valid = QMetaObject::invokeMethod(contents, "activated", Q_ARG(int, count - 1)) && valid;
            valid = valid && page->property("lastStep").toBool();
            valid = QMetaObject::invokeMethod(verifyTutorialSkip ? skip : next, "clicked") && valid;
            valid = valid && !tutorial.required() && window->property("currentPage").toInt() == 0;
            nookbit::TutorialController restored(uiPreferences, true);
            valid = valid && !restored.required();
            auto* reopen = findUiChild<QObject*>(window, "settingsTutorialButton");
            auto* close = findUiChild<QObject*>(window, "extensionCloseButton");
            valid = valid && reopen && close;
            if (reopen && close) {
                window->setProperty("currentPage", 8);
                valid = QMetaObject::invokeMethod(reopen, "clicked") && valid;
                valid = valid && tutorial.reviewing() && page->property("stepIndex").toInt() == 0;
                valid = QMetaObject::invokeMethod(next, "clicked") && valid;
                valid = QMetaObject::invokeMethod(close, "clicked") && valid;
                valid = valid && window->property("currentPage").toInt() == 8 && !tutorial.required();
                valid = QMetaObject::invokeMethod(reopen, "clicked") && valid;
                valid = valid && page->property("stepIndex").toInt() == 0;
                valid = QMetaObject::invokeMethod(skip, "clicked") && valid;
            }
        }
        valid = valid && organizer && companion && organizer->preview().defaultDestinationRoot() == beforeFolder
            && organizer->preview().activeRuleLabel() == beforeRules && companion->window().shown() == beforeShown
            && organizer->pendingFiles().count() == beforeCount && !features.busy() && featureBindingsClean;
        QTextStream(stdout) << "Tutorial " << (verifyTutorialSkip ? "skip" : "finish") << ": " << valid << Qt::endl;
        return valid ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    // 활성 기능 조합 검사는 실제 창구와 화면 부재를 확인하고 사용자 저장소에 접근하지 않는다.
    if (verifyFeatures) {
        QEventLoop settle;
        QTimer::singleShot(160, &settle, &QEventLoop::quit);
        settle.exec();
        auto* window = engine.rootObjects().isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        bool valid = window && featureBindingsClean && !features.busy();
        valid = valid && bool(organizer) == startup.active.organizer && bool(folderWatch) == startup.active.watch
            && bool(notificationRuntime) == startup.active.notifications && bool(companion) == startup.active.companion
            && bool(features.feeding()) == startup.active.feeding;
        if (companion) {
            const auto actions = companion->interactions().menuActions();
            valid = valid && actions.size() == (startup.active.feeding ? 1 : 0);
        }
        const int expectedPages = (organizer ? 5 : 0) + (folderWatch ? 1 : 0)
            + (notificationRuntime ? 1 : 0) + (companion ? 1 : 0);
        valid = valid && uiPages.pages().size() == expectedPages;
        const auto tutorialSteps = uiPages.tutorialSteps();
        valid = valid && tutorialSteps.size() == 2 + (organizer ? 3 : 0) + (folderWatch ? 1 : 0)
            + (notificationRuntime ? 1 : 0) + (companion ? 1 : 0);
        for (const auto& value : tutorialSteps) {
            const auto owner = value.toMap().value("ownerId").toString();
            valid = valid && (owner == "host" || (owner == "file-organizer" && organizer)
                || (owner == "folder-watch" && folderWatch) || (owner == "notifications" && notificationRuntime)
                || (owner == "companion" && companion));
        }
        if (notificationRuntime) valid = valid && notificationRuntime->controller().types().size() == (folderWatch ? 1 : 0);
        const auto present = [window](const char* name) { return findUiChild<QObject*>(window, QString::fromLatin1(name)) != nullptr; };
        valid = valid && present("fileSortPage") == startup.active.organizer && present("ruleEditorPage") == startup.active.organizer
            && present("legacySortConfirmation") == startup.active.organizer && present("developerTools") == startup.active.organizer
            && present("watchSettingsDialog") == startup.active.watch && present("settingsWatchButton") == startup.active.watch
            && present("watchProposalDialog") == startup.active.watch && present("notificationSettingsPage") == startup.active.notifications
            && present("desktopCharacterWindow") == startup.active.companion && present("homeCompanionSprite") == startup.active.companion;
        valid = valid && present("companionAction_companion.feed") == startup.active.feeding;
        int companionActions{};
        for (auto* action : trayMenu.actions()) if (action->objectName() == "companionVisibilityAction") ++companionActions;
        valid = valid && companionActions == (startup.active.companion ? 1 : 0);
        if (window) {
            auto* catalogButton = findUiChild<QObject*>(window, "homeNavigation14");
            auto* settingsButton = findUiChild<QObject*>(window, "homeNavigation8");
            valid = valid && catalogButton && settingsButton && present("homeNavigationFooter")
                && !present("homeNavigation12") && !present("homeNavigation11");
            if (catalogButton) {
                valid = QMetaObject::invokeMethod(catalogButton, "clicked") && valid;
                valid = valid && window->property("currentPage").toInt() == 14 && present("allFeaturesPage")
                    && present("featureNavigation7") == startup.active.organizer
                    && present("featureNavigation1") == startup.active.organizer
                    && present("featureNavigation4") == startup.active.organizer
                    && present("featureNavigation3") == startup.active.watch
                    && present("featureNavigation5") == startup.active.companion
                    && present("featureNavigation11") == startup.active.notifications
                    && !present("featureNavigation10") && !present("featureNavigation6");
                auto* moduleButton = findUiChild<QObject*>(window, "featureNavigation12");
                valid = valid && moduleButton;
                if (moduleButton) valid = QMetaObject::invokeMethod(moduleButton, "clicked") && valid;
                valid = valid && window->property("currentPage").toInt() == 12;
                if (settingsButton) valid = QMetaObject::invokeMethod(settingsButton, "clicked") && valid;
                valid = valid && window->property("currentPage").toInt() == 8;
            }
            window->setProperty("currentPage", 8);
            QCoreApplication::processEvents();
            valid = valid && present("settingsPage") && present("globalDarkModeSwitch");
            auto* versionLabel = findUiChild<QObject*>(window, "applicationVersionLabel");
            valid = valid && versionLabel && versionLabel->property("text").toString() == "NookBit " + QCoreApplication::applicationVersion();
            valid = valid && present("featureStartupMessage") == !startup.messages.isEmpty();
            auto* tutorialButton = findUiChild<QObject*>(window, "settingsTutorialButton");
            auto* tutorialPage = findUiChild<QObject*>(window, "tutorialPage");
            valid = valid && tutorialButton && tutorialPage;
            if (tutorialButton && tutorialPage) {
                valid = QMetaObject::invokeMethod(tutorialButton, "clicked") && valid;
                valid = valid && tutorialPage->property("stepCount").toInt() == tutorialSteps.size();
                valid = tutorial.skip() && valid;
            }
            auto* settingsModules = findUiChild<QObject*>(window, "settingsModulesButton");
            valid = valid && settingsModules;
            if (settingsModules) valid = QMetaObject::invokeMethod(settingsModules, "clicked") && valid;
            QCoreApplication::processEvents();
            valid = valid && window->property("currentPage").toInt() == 12 && present("moduleManagementPage")
                && present("moduleToggle_file-organizer") && present("moduleSaveButton");
            // 임시 저장 복원 검사에서 실제 화면 요청이 저장만 하고 기능을 즉시 교체하지 않는지 확인한다.
            if (verifyFeaturePreferences) {
                auto* toggle = findUiChild<QObject*>(window, "moduleToggle_companion");
                if (toggle) {
                    toggle->setProperty("checked", !savedFeatures.companion);
                    valid = QMetaObject::invokeMethod(toggle, "toggled") && valid && featureSettings.dirty();
                } else valid = false;
                auto* saveButton = findUiChild<QObject*>(window, "moduleSaveButton");
                valid = valid && saveButton && saveButton->property("enabled").toBool();
                if (saveButton) valid = QMetaObject::invokeMethod(saveButton, "clicked") && valid;
                const auto saved = featurePreferences.load();
                valid = valid && saved && saved.value().companion != savedFeatures.companion && !featureSettings.dirty()
                    && featureSettings.restartRequired() && present("desktopCharacterWindow") == startup.active.companion;
                featureSettings.setEnabled("companion", savedFeatures.companion);
                valid = featureSettings.save() && valid && !featureSettings.restartRequired();
            }
            if (!organizer) {
                window->setProperty("currentPage", 9);
                valid = valid && window->property("currentPage").toInt() == 0;
            }
            for (const int route : {1, 3, 4, 5, 6, 7, 10, 11}) if (!uiPages.contains(route)) {
                window->setProperty("currentPage", route);
                QCoreApplication::processEvents();
                valid = valid && window->property("currentPage").toInt() == 0;
            }
        }
        valid = valid && featureBindingsClean;
        QTextStream(stdout) << "Feature startup: organizer=" << startup.active.organizer << " watch=" << startup.active.watch
            << " notifications=" << startup.active.notifications << " companion=" << startup.active.companion
            << " feeding=" << startup.active.feeding << " valid=" << valid << Qt::endl;
        return valid ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    // 자동 검사에서는 화면과 리소스를 확인하고 이벤트를 한 번 처리한 뒤 종료한다.
    if (verifyUi) {
        auto& pendingFiles = organizer->pendingFiles();
        auto& backgroundTasks = organizer->tasks();
        auto& previewController = organizer->preview();
        auto& historyController = organizer->history();
        auto& recoveryController = organizer->recovery();
        auto& ruleEditor = organizer->rules();
        auto& watchController = folderWatch->controller();
        auto& notifications = notificationRuntime->controller();
        auto& characterController = companion->character();
        auto& characterSettings = companion->settings();
        auto& characterWindow = companion->window();
        auto& companionEdits = companion->edits();
        auto& companionInteractions = companion->interactions();
        const auto migrationPath = QStringLiteral(":/nookbit/001_initial.sql");
        const auto bundledTree = QStringLiteral(":/nookbit/default-v3.json");
        const auto treeSchema = QStringLiteral(":/nookbit/rules-tree-v3.schema.json");
        if (!verifyBundledHistorySchema(migrationPath)) return EXIT_FAILURE;
        bool compactLayout = false;
        if (!engine.rootObjects().isEmpty()) {
            auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
            auto* home = window ? findUiChild<QObject*>(window, QStringLiteral("compactHome")) : nullptr;
            auto* developer = window ? findUiChild<QObject*>(window, QStringLiteral("developerTools")) : nullptr;
            auto* extension = window ? findUiChild<QQuickWindow*>(window, QStringLiteral("extensionWindow")) : nullptr;
            if (window && home && developer && extension) {
                // 다른 창의 배치·노출 문제를 검사 단계 이름으로 식별하는 함수.
                const auto checkLayout = [&compactLayout](bool condition, const char* stage) {
                    if (!condition) { compactLayout = false; QTextStream(stderr) << "Layout check failed: " << stage << Qt::endl; }
                };
                // 본체 크기를 유지하는 별도 창·프레임 제거·연결 위치·개발 도구 숨김을 검사한다.
                compactLayout = true;
                auto* rulePage = findUiChild<QQuickItem*>(window, QStringLiteral("ruleEditorPage"));
                auto* closeButton = findUiChild<QQuickItem*>(window, QStringLiteral("extensionCloseButton"));
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
                window->setProperty("currentPage", 3);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == settingsSize, "watch subpage preserves settings window size");
                window->setProperty("currentPage", 8);
                QCoreApplication::processEvents();
                checkLayout(extension->size() == settingsSize, "return from watch preserves settings window size");
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
                auto* filePage = findUiChild<QQuickItem*>(window, QStringLiteral("fileSortPage"));
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
                auto* recoveryPage = findUiChild<QQuickItem*>(window, QStringLiteral("recoveryDialog"));
                auto* recoveryConfirm = findUiChild<QObject*>(window, QStringLiteral("recoveryConfirmDialog"));
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
                auto* undoButton = findUiChild<QQuickItem*>(window, QStringLiteral("compactUndoButton"));
                checkLayout(undoButton && undoButton->width() >= undoButton->implicitWidth() && undoButton->width() >= 60, "undo label width");
                auto* previewButton = findUiChild<QQuickItem*>(window, QStringLiteral("compactPreviewButton"));
                auto* sortButton = findUiChild<QQuickItem*>(window, QStringLiteral("compactSortButton"));
                checkLayout(previewButton && sortButton && undoButton
                    && std::abs(previewButton->width() - sortButton->width()) <= 1
                    && previewButton->height() == uiMetrics->property("largeActionHeight").toInt()
                    && sortButton->height() == previewButton->height()
                    && undoButton->mapToScene(QPointF(undoButton->width(), 0)).x() <= previewButton->mapToScene(QPointF()).x()
                    && std::abs(undoButton->mapToScene(QPointF(0, undoButton->height() / 2)).y()
                        - sortButton->mapToScene(QPointF(0, sortButton->height() / 2)).y()) <= 1,
                    "large paired actions and separate undo on fixed footer");
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
                checkLayout(verifyHomeMenuAlignment(window, uiMetrics), "left aligned home menu text and icons");
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
            : findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("ruleEditorPage"));
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
            : findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("watchSettingsDialog"));
        QObject* historyDialog = engine.rootObjects().isEmpty() ? nullptr
            : findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("historyDialog"));
        QObject* recoveryDialog = engine.rootObjects().isEmpty() ? nullptr
            : findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("recoveryDialog"));
        QObject* recoveryConfirmDialog = engine.rootObjects().isEmpty() ? nullptr
            : findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("recoveryConfirmDialog"));
        auto* companionExtension = engine.rootObjects().isEmpty() ? nullptr
            : findUiChild<QQuickWindow*>(engine.rootObjects().first(), QStringLiteral("extensionWindow"));
        QObject* characterSettingsDialog = findVisualItem(companionExtension ? companionExtension->contentItem() : nullptr,
            QStringLiteral("characterSettingsDialog"));
        auto* desktopCharacter = engine.rootObjects().isEmpty() ? nullptr
            : findUiChild<QQuickWindow*>(engine.rootObjects().first(), QStringLiteral("desktopCharacterWindow"));
        QObject* desktopSprite = desktopCharacter
            ? desktopCharacter->findChild<QObject*>(QStringLiteral("desktopCharacterSprite")) : nullptr;
        auto* characterBubble = desktopCharacter
            ? desktopCharacter->findChild<QQuickWindow*>(QStringLiteral("characterBubbleWindow")) : nullptr;
        // 인터넷 없이 두 제작 문서를 읽고 직접 저장 창구로 제공하는지 검사한다.
        auto* characterGuide = characterSettingsDialog
            ? characterSettingsDialog->findChild<QObject*>(QStringLiteral("characterGuideDownload")) : nullptr;
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
                           findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("compactHome")) != nullptr &&
                           findUiChild<QObject*>(engine.rootObjects().first(), QStringLiteral("extensionPanel")) != nullptr &&
                           characterSettingsDialog != nullptr &&
                           characterSettingsDialog->findChild<QObject*>(QStringLiteral("characterAnchorEditor")) != nullptr &&
                           QMetaObject::invokeMethod(characterSettingsDialog->findChild<QObject*>(QStringLiteral("characterAnchorEditor")), "open") &&
                           QMetaObject::invokeMethod(characterSettingsDialog->findChild<QObject*>(QStringLiteral("characterAnchorEditor")), "close") &&
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

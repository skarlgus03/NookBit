#include "presentation/ui_verification.hpp"
#include "presentation/pending_files_model.hpp"
#include "presentation/file_preview_provider.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/ui_settings_controller.hpp"
#include "infrastructure/persistence_sqlite/sqlite_schema.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFont>
#include <QPalette>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QSqlQuery>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QSemaphore>
#include <QJSValue>
#include <cmath>
#include <memory>

// 격리된 UI 검사에서 명시적으로 요청한 화면만 PNG로 저장하는 함수.
bool captureVerificationScreens(QQuickWindow* root, QObject* metrics, nookbit::UiSettingsController& settings, const QString& directory) {
    if (!root || !metrics || directory.isEmpty() || !QDir().mkpath(directory)) return false;
    auto* extension = root->findChild<QQuickWindow*>(QStringLiteral("extensionWindow"));
    auto* home = root->findChild<QObject*>(QStringLiteral("compactHome"));
    if (!extension || !home) return false;
    root->setPosition(40, 40);
    for (const bool dark : {false, true}) {
        settings.setDarkMode(dark);
        const auto suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
        for (const int page : {7, 8}) {
            QMetaObject::invokeMethod(home, "pageRequested", Q_ARG(int, page));
            const auto spec = metrics->property("extensionDefaults").value<QJSValue>().property(page);
            extension->resize(spec.property("width").toInt(), spec.property("height").toInt());
            QEventLoop frame;
            QTimer::singleShot(300, &frame, &QEventLoop::quit);
            frame.exec();
            const auto image = extension->grabWindow();
            const auto name = (page == 7 ? QStringLiteral("file-sort-") : QStringLiteral("settings-")) + suffix + ".png";
            if (image.isNull() || !image.save(QDir(directory).filePath(name))) return false;
        }
        const auto image = root->grabWindow();
        if (image.isNull() || !image.save(QDir(directory).filePath("home-" + suffix + ".png"))) return false;
    }
    return true;
}

// 내장 SQL로 임시 메모리 DB를 최신 스키마까지 만들 수 있는지 검사하는 함수.
bool verifyBundledHistorySchema(const QString& migrationPath) {
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

// 실제 확장 창에 마우스 이벤트를 보내 모든 페이지의 접기 버튼 접근성을 검사하는 함수.
bool verifyExtensionInteraction(QQuickWindow* mainWindow, QQuickWindow* extension, QObject* metrics) {
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
        QCoreApplication::sendEvent(extension, &press);
        // Qt 포인터 상태가 놓기로 덮이기 전에 누름 이벤트를 먼저 처리한다.
        QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(extension, &release);
        QCoreApplication::processEvents();
    };
    for (const int page : {1, 3, 4, 5, 6, 7, 8, 9, 10}) {
        mainWindow->setProperty("currentPage", page);
        QCoreApplication::processEvents();
        auto* home = mainWindow->findChild<QObject*>(QStringLiteral("compactHome"));
        const int expectedMenu = (page == 3 || page == 5 || page == 6 || page == 8 || page == 9 || page == 10) ? 8 : page;
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
            QMetaObject::invokeMethod(addMenu, "close");
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
            const auto titles = mainWindow->findChildren<QQuickItem*>(QStringLiteral("settingsSectionTitle"));
            const auto dividers = mainWindow->findChildren<QQuickItem*>(QStringLiteral("settingsSectionDivider"));
            if (titles.size() != 3 || dividers.size() != 3) return false;
            for (auto* title : titles) {
                if (title->property("font").value<QFont>().pixelSize() != metrics->property("textSectionTitle").toInt() ||
                    !title->isVisible() || title->height() <= 0) return false;
            }
            for (auto* divider : dividers) {
                if (!divider->isVisible() || divider->height() != metrics->property("borderNormal").toInt() || divider->width() <= 0) return false;
            }
        }
        click(button);
        if (mainWindow->property("currentPage").toInt() != 0 || extension->isVisible()) {
            QTextStream(stderr) << "Extension close click blocked on page " << page << Qt::endl;
            return false;
        }
    }
    return true;
}

// 파일 정리의 임시 미리보기가 창·입력 상태를 보존하며 돌아가기와 Esc로 닫히는지 검사하는 함수.
bool verifyInlineFilePreview(QQuickWindow* window, QQuickItem* page) {
    auto* button = page->findChild<QObject*>(QStringLiteral("compactPreviewButton"));
    auto* preview = page->findChild<QQuickItem*>(QStringLiteral("fileSortPreview"));
    auto* back = page->findChild<QQuickItem*>(QStringLiteral("filePreviewBackButton"));
    auto* destination = page->findChild<QObject*>(QStringLiteral("compactDestinationField"));
    // 실패한 화면 단계와 입력 상태를 남겨 자동 검사 원인을 구분하는 함수.
    const auto fail = [page](const char* stage) {
        QTextStream(stderr) << "Inline preview check failed at " << stage
            << ": step=" << page->property("workflowStep").toInt()
            << " transition=" << page->property("transitioning").toBool() << Qt::endl;
        return false;
    };
    if (!button || !preview || !back || !destination) return fail("controls");
    const auto size = window->size();
    const auto position = window->position();
    const auto windows = QGuiApplication::allWindows().size();
    const auto text = destination->property("text").toString();
    auto* controller = preview->property("controller").value<QObject*>();
    if (!controller) return fail("controller");
    // 비동기 미리보기 완료와 뒤따르는 화면 전환을 각각 기다리는 함수.
    const auto waitPreview = [controller] {
        QElapsedTimer elapsed; elapsed.start();
        while (controller->property("previewBusy").toBool() && elapsed.elapsed() < 5000) {
            QEventLoop loop;
            QTimer::singleShot(10, &loop, &QEventLoop::quit);
            loop.exec();
        }
        return !controller->property("previewBusy").toBool();
    };
    QMetaObject::invokeMethod(button, "clicked");
    if (!waitPreview()) return fail("preview timeout");
    QEventLoop layout;
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    auto* input = page->findChild<QQuickItem*>(QStringLiteral("fileSortScroll"));
    if (!preview->isVisible() || preview->window() != window || window->size() != size
        || window->position() != position || QGuiApplication::allWindows().size() != windows
        || window->transientParent()->property("currentPage").toInt() != 7
        || !input || input->isVisible() || std::abs(preview->x()) > 1
        || page->property("transitioning").toBool()) {
        QTextStream(stderr) << "Preview presentation failed: step=" << page->property("workflowStep").toInt()
            << " visible=" << preview->isVisible() << " x=" << preview->x()
            << " windows=" << QGuiApplication::allWindows().size() << "/" << windows
            << " size=" << window->width() << "x" << window->height()
            << " position=" << window->x() << "," << window->y()
            << " message=" << controller->property("message").toString() << Qt::endl;
        return false;
    }
    const auto point = back->mapToScene(QPointF(back->width() / 2, back->height() / 2));
    const QPointF global = window->mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress, point, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, point, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &release);
    if (page->property("previewOpen").toBool() || destination->property("text").toString() != text) {
        QTextStream(stderr) << "Back button: enabled=" << back->isEnabled() << " visible=" << back->isVisible()
            << " point=" << point.x() << "," << point.y() << " size=" << back->width() << "x" << back->height() << Qt::endl;
        return fail("back click");
    }
    // 실패한 검사에서는 다음 화면을 열거나 이전 결과를 재사용하지 않는다.
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    destination->setProperty("text", QStringLiteral("relative-destination"));
    QMetaObject::invokeMethod(button, "clicked");
    if (page->property("workflowStep").toInt() != 0 || controller->property("hasPreview").toBool()
        || controller->property("message").toString().isEmpty()) return fail("invalid preview");
    destination->setProperty("text", text);
    QMetaObject::invokeMethod(button, "clicked");
    if (!waitPreview()) return fail("second preview timeout");
    window->requestActivate();
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QKeyEvent escapeRelease(QEvent::KeyRelease, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(window, &escape);
    QCoreApplication::sendEvent(window, &escapeRelease);
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    // 백그라운드 알림만으로는 화면이 바뀌지 않고 수동 요청에서만 단계가 전환되는지 검사한다.
    QMetaObject::invokeMethod(controller, "sortExecutionStarted");
    if (page->property("workflowStep").toInt() != 0) return fail("escape");
    page->setProperty("manualSortRequested", true);
    QMetaObject::invokeMethod(controller, "sortExecutionStarted");
    if (page->property("workflowStep").toInt() != 3) return fail("manual execution");
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    QQuickItem* progress = nullptr;
    QQuickItem* complete = nullptr;
    for (auto* item : page->childItems()) {
        if (item->objectName() == QStringLiteral("fileSortProgress")) progress = item;
        if (item->objectName() == QStringLiteral("fileSortComplete")) complete = item;
    }
    if (!progress || !complete || !progress->isVisible() || input->isVisible() || preview->isVisible()) return fail("progress");
    QMetaObject::invokeMethod(controller, "sortExecutionFinished", Q_ARG(bool, false), Q_ARG(QString, QString("test failure")));
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    if (page->property("workflowStep").toInt() != 4 || !complete->isVisible()
        || complete->property("success").toBool() || progress->isVisible()) return fail("completion");
    auto* doneBack = complete->findChild<QObject*>(QStringLiteral("fileCompleteBackButton"));
    if (!doneBack || doneBack->property("text").toString().length() != 0) return fail("completion back");
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
        || oldDialog->property("visible").toBool() || window->size() != size) return fail("confirmation");
    QMetaObject::invokeMethod(confirmBack, "clicked");
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    if (page->property("workflowStep").toInt() != 1 || !preview->isVisible()) return fail("confirmation back");
    QCoreApplication::sendEvent(window, &escape);
    QCoreApplication::sendEvent(window, &escapeRelease);
    QTimer::singleShot(250, &layout, &QEventLoop::quit);
    layout.exec();
    return !page->property("previewOpen").toBool() && window->size() == size
        && input->isVisible() && !preview->isVisible() && !page->property("transitioning").toBool()
        && destination->property("text").toString() == text;
}

// 아이콘과 글씨 길이에 관계없이 메뉴 글씨 중심이 버튼 전체 중심과 일치하는지 검사하는 함수.
bool verifyHomeMenuAlignment(QQuickWindow* root, QObject* metrics) {
    auto* home = root->findChild<QQuickItem*>(QStringLiteral("compactHome"));
    if (!home) return false;
    // Repeater의 대리 항목을 포함한 실제 화면 트리에서 이름으로 찾는 함수.
    const auto find = [](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems()) if (auto* found = self(self, child, name)) return found;
        return nullptr;
    };
    for (const int page : {7, 1, 4, 8}) {
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
bool verifyBusyPresentation(QQuickWindow* root, QQuickWindow* extension, nookbit::BackgroundTaskRunner& runner) {
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
bool verifyPageFileDrop(QQuickWindow* window, QQuickItem* page, nookbit::PendingFilesModel& pending) {
    QTemporaryDir directory;
    if (!directory.isValid() || !page) return false;
    const auto path = directory.filePath(QStringLiteral("report.pdf"));
    QFile fixture(path);
    if (!fixture.open(QIODevice::WriteOnly) || fixture.write("unchanged") != 9) return false;
    fixture.close();
    auto* area = page->findChild<QQuickItem*>(QStringLiteral("fileSortPageDropArea"));
    if (!area || area->width() != page->width() || area->height() != page->height()) return false;
    auto* addFiles = page->findChild<QQuickItem*>(QStringLiteral("addPendingFilesButton"));
    auto* chooseFolder = page->findChild<QQuickItem*>(QStringLiteral("chooseDestinationButton"));
    if (!addFiles || !chooseFolder || !addFiles->isEnabled() || !chooseFolder->isEnabled()
        || addFiles->width() <= 0 || chooseFolder->width() <= 0) return false;
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


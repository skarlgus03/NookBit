#include "presentation/ui_font.hpp"
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QFontDatabase>
#include <QFontInfo>
#include <QQuickStyle>
#include <QTextStream>
#include <memory>

// 잘못된 시스템 스타일을 주입해도 공통 글꼴과 페이지 제목 굵기가 유지되는지 검사하는 함수.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QQuickStyle::setStyle("Basic");
    QFont poisoned("Times New Roman");
    poisoned.setStyleName("Thin"); poisoned.setWeight(QFont::Thin);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    poisoned.setVariableAxis("wght", 100);
#endif
    app.setFont(poisoned);
    QQmlEngine engine;
    QQmlComponent metricsComponent(&engine, QUrl::fromLocalFile(QStringLiteral(NOOKBIT_SOURCE_DIR "/src/presentation/qml/UiMetrics.qml")));
    std::unique_ptr<QObject> metrics(metricsComponent.create());
    if (!metrics) return 1;
    const auto font = nookbit::applicationUiFont(*metrics);
    if (!font.styleName().isEmpty() || font.weight() != metrics->property("bodyWeight").toInt()
        || font.pixelSize() != metrics->property("textBody").toInt() || font.italic()) return 1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    if (!font.variableAxisTags().isEmpty()) return 1;
#endif
    app.setFont(font);
    metrics->setProperty("applicationFont", font);
    engine.rootContext()->setContextProperty("UiMetrics", metrics.get());
    QQmlComponent windowComponent(&engine);
    windowComponent.setData(R"(
import QtQuick
import QtQuick.Controls
ApplicationWindow {
    id: main
    font: UiMetrics.applicationFont
    Label { objectName: "mainBody"; text: "본문"; font.pixelSize: UiMetrics.textBody }
    ApplicationWindow {
        objectName: "extension"
        font: main.font
        Item {
            Label { objectName: "pageTitle"; text: "모듈 관리"; font.pixelSize: UiMetrics.textWindowTitle; font.bold: true }
            Loader {
                active: true
                sourceComponent: Component {
                    Label { objectName: "loadedBody"; text: "필요한 기능만 켜 두세요"; font.pixelSize: UiMetrics.textBody }
                }
            }
        }
    }
})", {});
    std::unique_ptr<QObject> root(windowComponent.create());
    if (!root) { QTextStream(stderr) << windowComponent.errorString(); return 1; }
    // 창이 생긴 뒤 바뀐 전역 스타일도 명시적인 창 글꼴을 덮지 못하는지 확인한다.
    app.setFont(poisoned);
    QCoreApplication::processEvents();
    auto* extension = root->findChild<QObject*>("extension");
    auto* title = root->findChild<QObject*>("pageTitle");
    auto* loaded = root->findChild<QObject*>("loadedBody");
    auto* body = root->findChild<QObject*>("mainBody");
    if (!extension || !title || !loaded || !body) return 1;
    for (auto* object : {root.get(), extension, body, loaded, title}) {
        const auto actual = object->property("font").value<QFont>();
        const auto expectedWeight = object == title ? QFont::Bold : QFont::Normal;
        if (actual.families() != font.families() || !actual.styleName().isEmpty() || actual.weight() != expectedWeight) {
            QTextStream(stderr) << "Font inheritance failed: " << object->objectName() << " " << actual.toString() << Qt::endl;
            return 1;
        }
        if (QFontDatabase::hasFamily(font.families().first())) {
            const QFontInfo resolved(actual);
            if (resolved.family() != font.families().first() || resolved.weight() != expectedWeight) {
                QTextStream(stderr) << "Font matching failed: " << object->objectName() << " " << resolved.family() << " " << resolved.weight() << Qt::endl;
                return 1;
            }
        }
    }
    app.setFont(font);
    QTextStream(stdout) << "UI font family and regular/bold weights remain stable across windows and loaders." << Qt::endl;
    return 0;
}

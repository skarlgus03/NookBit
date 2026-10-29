#include "presentation/ui_font.hpp"
#include <QApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QFontDatabase>
#include <QFontInfo>
#include <QQuickStyle>
#include <QTextStream>
#include <QTextLayout>
#include <QGlyphRun>
#include <QImage>
#include <QPainter>
#include <array>
#include <memory>

// 잘못된 시스템 스타일을 주입해도 공통 글꼴과 페이지 제목 굵기가 유지되는지 검사하는 함수.
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    nookbit::configureUiTextRendering();
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
    if (!nookbit::loadApplicationUiFonts(*metrics)) return 1;
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
            // 지정값뿐 아니라 한글 문장을 실제로 구성한 글리프의 굵기도 확인한다.
            QTextLayout layout(QStringLiteral("모듈 관리 필요한 기능만 켜 두세요"), actual);
            layout.beginLayout();
            auto line = layout.createLine();
            if (!line.isValid()) return 1;
            line.setLineWidth(2000);
            layout.endLayout();
            const auto runs = layout.glyphRuns();
            if (runs.isEmpty()) return 1;
            for (const auto& run : runs) {
                const auto raw = run.rawFont();
                if (raw.familyName() != font.families().first() || raw.weight() != expectedWeight
                    || !raw.fontTable("fvar").isEmpty()) return 1;
            }
        }
    }
    // 나중에 로드한 페이지도 확대 가능한 기본 렌더러를 사용하는지 확인한다.
    for (auto* label : {body, title, loaded}) {
        if (label->property("renderType").toInt() != QQuickWindow::QtTextRendering) return 1;
    }
    // 메타데이터만 바뀌고 실제 획은 같은 굵기로 그려지는 문제도 검출한다.
    std::array<quint64, 3> ink{};
    const std::array<QFont::Weight, 3> weights{QFont::Normal, QFont::Medium, QFont::Bold};
    for (std::size_t i = 0; i < weights.size(); ++i) {
        auto sampleFont = font;
        sampleFont.setWeight(weights[i]);
        sampleFont.setPixelSize(48);
        const auto raw = QRawFont::fromFont(sampleFont);
        if (!raw.fontTable("fvar").isEmpty() || raw.weight() != weights[i]) return 1;
        QImage image(1000, 100, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            painter.setFont(sampleFont);
            painter.setPen(Qt::white);
            painter.drawText(image.rect(), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("모듈 관리 파일 정리 NookBit"));
        }
        for (int y = 0; y < image.height(); ++y) {
            const auto* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x) ink[i] += qAlpha(pixels[x]);
        }
    }
    if (!(ink[0] > 0 && ink[0] < ink[1] && ink[1] < ink[2])) {
        QTextStream(stderr) << "Static font raster weights are not distinct: " << ink[0] << ", " << ink[1] << ", " << ink[2] << Qt::endl;
        return 1;
    }
    app.setFont(font);
    QTextStream(stdout) << "UI font family and regular/bold weights remain stable across windows and loaders." << Qt::endl;
    return 0;
}

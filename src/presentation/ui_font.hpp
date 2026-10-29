#pragma once
#include <QFont>
#include <QFontDatabase>
#include <QObject>
#include <QQuickWindow>
#include <QVariant>

namespace nookbit {
/// 확대되는 캔버스도 같은 품질을 유지하도록 Qt의 기본 글자 그리기를 사용하는 함수.
inline void configureUiTextRendering() {
    QQuickWindow::setTextRenderType(QQuickWindow::QtTextRendering);
}

/// 시스템에 설치된 동명 글꼴과 섞이지 않는 내장 고정 굵기 글꼴을 등록하는 함수.
inline bool loadApplicationUiFonts(const QObject& metrics) {
    const auto family = metrics.property("fontFamily").toString();
    const auto files = metrics.property("fontFiles").toStringList();
    if (files.isEmpty()) return false;
    for (const auto& file : files) {
        const auto id = QFontDatabase::addApplicationFont(file);
        if (id < 0 || !QFontDatabase::applicationFontFamilies(id).contains(family)) return false;
    }
    return true;
}

/// 실행 환경의 이름 있는 스타일·가변 축을 물려받지 않고 공통 규격으로 글꼴을 만드는 함수.
inline QFont applicationUiFont(const QObject& metrics) {
    QFont font;
    font.setFamilies({metrics.property("fontFamily").toString(), metrics.property("fontFallback").toString()});
    font.setStyleName(QString());
    font.setStyle(QFont::StyleNormal);
    font.setStyleHint(QFont::SansSerif);
    font.setStretch(QFont::Unstretched);
    font.setWeight(static_cast<QFont::Weight>(metrics.property("bodyWeight").toInt()));
    font.setPixelSize(metrics.property("textBody").toInt());
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    font.clearVariableAxes();
#endif
    return font;
}
}

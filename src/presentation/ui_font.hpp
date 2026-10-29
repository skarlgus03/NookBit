#pragma once
#include <QFont>
#include <QObject>
#include <QQuickWindow>
#include <QVariant>

namespace nookbit {
/// Windows에서는 가변 글꼴의 GPU 거리장 경로 대신 운영체제의 글자 그리기를 사용하는 함수.
inline void configureUiTextRendering() {
#ifdef Q_OS_WIN
    QQuickWindow::setTextRenderType(QQuickWindow::NativeTextRendering);
#endif
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

#pragma once
#include <QFont>
#include <QObject>
#include <QVariant>

namespace nookbit {
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

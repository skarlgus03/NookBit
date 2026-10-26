import QtQuick
import QtQuick.Controls

// 미리보기·정리·최종 확인에 사용하는 공통 큰 실행 버튼. 역할만 다르고 규격은 같다.
AppButton {
    id: control
    implicitHeight: UiMetrics.largeActionHeight
    implicitWidth: Math.max(UiMetrics.buttonMinWidth, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: UiMetrics.textEmphasis
    font.bold: true
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.controlPadding
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.buttonForeground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: ButtonFeedback {
        button: control
        primary: control.primary
        radius: UiMetrics.radiusCard
    }
}

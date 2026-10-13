import QtQuick
import QtQuick.Controls

// 미리보기·정리·최종 확인에 사용하는 공통 큰 실행 버튼. 역할만 다르고 규격은 같다.
AppButton {
    id: control
    property bool primary: false
    implicitHeight: UiMetrics.largeActionHeight
    implicitWidth: Math.max(UiMetrics.buttonMinWidth, contentItem.implicitWidth + leftPadding + rightPadding)
    font.pixelSize: UiMetrics.textEmphasis
    font.bold: true
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.controlPadding
    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled ? control.palette.disabled.buttonText : control.primary ? UiTheme.light.accentText : control.palette.buttonText
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    background: Rectangle {
        radius: UiMetrics.radiusCard
        color: control.primary && control.enabled ? UiTheme.light.accent : control.palette.button
        border.color: control.hovered || control.visualFocus ? UiTheme.light.primaryFocus : control.palette.mid
        border.width: control.hovered || control.visualFocus ? UiMetrics.borderActive : UiMetrics.borderNormal
    }
}

import QtQuick
import QtQuick.Controls

// 공통 크기와 테마로 값의 위치를 표시하는 가로 슬라이더.
Slider {
    id: control
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    implicitHeight: UiMetrics.controlHeight
    leftPadding: UiMetrics.spaceTiny
    rightPadding: UiMetrics.spaceTiny
    background: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: control.availableWidth
        height: UiMetrics.sliderTrackHeight
        radius: height / 2
        color: control.theme.alternate
        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: control.theme.accent
        }
    }
    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: (control.height - height) / 2
        width: UiMetrics.sliderHandleSize
        height: UiMetrics.sliderHandleSize
        radius: width / 2
        color: control.pressed ? control.theme.softButton : control.theme.surface
        border.color: control.theme.accent
        border.width: UiMetrics.borderActive
    }
}

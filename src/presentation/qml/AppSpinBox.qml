import QtQuick
import QtQuick.Controls

// 숫자를 직접 입력하고 오른쪽의 둥근 버튼으로 조절하는 공통 입력 상자.
SpinBox {
    id: control
    implicitHeight: UiMetrics.controlHeight
    implicitWidth: UiMetrics.numericInputWidth
    font.pixelSize: UiMetrics.textBody
    leftPadding: UiMetrics.controlPadding
    rightPadding: UiMetrics.numericStepSize * 2 + UiMetrics.spaceExtraSmall + UiMetrics.buttonPadding
    hoverEnabled: true
    background: Rectangle {
        radius: UiMetrics.radiusControl
        color: control.palette.base
        border.color: control.activeFocus || control.hovered ? control.palette.highlight : control.palette.mid
        border.width: control.activeFocus || control.hovered ? UiMetrics.borderActive : UiMetrics.borderNormal
        opacity: control.enabled ? 1 : 0.6
    }
    component StepIndicator: Rectangle {
        required property bool increase
        readonly property bool available: control.enabled && (increase
            ? control.value < Math.max(control.from, control.to) : control.value > Math.min(control.from, control.to))
        readonly property bool pressed: increase ? control.up.pressed : control.down.pressed
        readonly property bool hovered: increase ? control.up.hovered : control.down.hovered
        readonly property var theme: UiTheme.colors(control.palette.window.hslLightness < 0.5)
        width: UiMetrics.numericStepSize
        height: UiMetrics.numericStepSize
        y: (control.height - height) / 2
        radius: width / 2
        color: !available ? control.palette.button : pressed ? theme.buttonPressed : hovered ? theme.buttonHover : control.palette.button
        border.color: available && (hovered || pressed) ? theme.buttonFocus : "transparent"
        border.width: UiMetrics.borderNormal
        opacity: available ? 1 : 0.4
        Behavior on color { ColorAnimation { duration: pressed ? 0 : UiMetrics.buttonTransitionMs } }
        Text {
            anchors.centerIn: parent
            text: parent.increase ? "+" : "−"
            font.pixelSize: UiMetrics.textEmphasis
            font.bold: true
            color: control.palette.text
        }
    }
    up.indicator: StepIndicator {
        objectName: "spinIncreaseIndicator"
        increase: true
        x: control.width - width - UiMetrics.buttonPadding
    }
    down.indicator: StepIndicator {
        objectName: "spinDecreaseIndicator"
        increase: false
        x: control.width - width * 2 - UiMetrics.spaceExtraSmall - UiMetrics.buttonPadding
    }
}

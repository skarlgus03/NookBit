import QtQuick
import QtQuick.Layouts

// 전달받은 작업 단계만 표시하며 승인·이동 요청은 만들지 않는 안내 표시.
Item {
    id: stepper
    objectName: "fileSortStepper"
    property int workflowStep: 0
    property bool dark: false
    readonly property int currentStep: Math.min(workflowStep, 3)
    readonly property var theme: UiTheme.colors(dark)
    implicitHeight: UiMetrics.fileSortStepperHeight
    implicitWidth: steps.implicitWidth
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("진행 단계: %1").arg([qsTr("대기 목록"), qsTr("미리보기"), qsTr("확인"), qsTr("이동")][currentStep])
    RowLayout {
        id: steps
        anchors.centerIn: parent
        spacing: UiMetrics.spaceControl
        Repeater {
            model: [qsTr("대기 목록"), qsTr("미리보기"), qsTr("확인"), qsTr("이동")]
            delegate: RowLayout {
                required property int index
                required property string modelData
                readonly property bool passed: index < stepper.currentStep
                readonly property bool active: index === stepper.currentStep
                spacing: UiMetrics.spaceSmall
                Rectangle {
                    visible: index > 0
                    Layout.preferredWidth: UiMetrics.fileSortStepConnectorWidth
                    Layout.preferredHeight: UiMetrics.borderNormal
                    Layout.rightMargin: UiMetrics.spaceExtraSmall
                    color: index <= stepper.currentStep ? stepper.theme.accent : stepper.theme.border
                }
                Rectangle {
                    Layout.preferredWidth: UiMetrics.fileSortStepCircleSize
                    Layout.preferredHeight: UiMetrics.fileSortStepCircleSize
                    radius: width / 2
                    color: active ? stepper.theme.accent : passed ? stepper.theme.softButton : "transparent"
                    border.width: active || passed ? 0 : UiMetrics.borderNormal
                    border.color: stepper.theme.border
                    NavigationIcon {
                        anchors.centerIn: parent
                        width: UiMetrics.iconSize
                        height: UiMetrics.iconSize
                        kind: "check"
                        strokeColor: stepper.theme.accentSoftText
                        visible: passed
                    }
                    Text {
                        anchors.centerIn: parent
                        text: index + 1
                        visible: !passed
                        color: active ? stepper.theme.accentText : stepper.theme.muted
                        font.pixelSize: UiMetrics.textBody
                        font.bold: active
                    }
                }
                Text {
                    text: modelData
                    font.pixelSize: UiMetrics.textBody
                    font.bold: active
                    color: active ? stepper.theme.accent : stepper.theme.muted
                }
            }
        }
    }
}

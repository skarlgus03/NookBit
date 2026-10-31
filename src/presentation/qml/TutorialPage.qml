import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 모듈이 제공한 텍스트 안내를 읽기 전용으로 보여주고 탐색만 관리하는 화면.
PageSurface {
    id: page
    objectName: "tutorialPage"
    required property var controller
    required property var steps
    property int stepIndex: 0
    property bool initialized: false
    property bool detailsExpanded: false
    readonly property int stepCount: steps.length
    readonly property var currentStep: stepCount > 0 ? steps[Math.min(stepIndex, stepCount - 1)] : ({})
    readonly property bool lastStep: stepIndex >= stepCount - 1
    // 다시 열 때 첫 안내와 스크롤 위치로 돌아가는 함수.
    function restart() { goTo(0) }
    // 안내 단계만 바꾸며 어떤 기능 명령도 호출하지 않는 함수.
    function goTo(index) {
        stepIndex = Math.max(0, Math.min(index, stepCount - 1))
        detailsExpanded = false
        scroll.contentItem.contentY = 0
    }
    Component.onCompleted: { initialized = true; restart() }
    onVisibleChanged: if (initialized && visible) restart()
    onStepsChanged: if (initialized) goTo(stepIndex)
    background: Rectangle { color: page.theme.window }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        spacing: UiMetrics.spaceSection
        Label {
            Layout.fillWidth: true
            text: qsTr("NookBit 사용법 · %1 / %2").arg(page.stepIndex + 1).arg(page.stepCount)
            font.pixelSize: UiMetrics.textEmphasis
            font.bold: true
            color: page.theme.accent
            Accessible.role: Accessible.StaticText
        }
        ProgressBar { Layout.fillWidth: true; from: 0; to: page.stepCount; value: page.stepIndex + 1 }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: scroll.availableWidth
                spacing: UiMetrics.settingsSectionSpacing
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: UiMetrics.spaceControl
                    Label {
                        objectName: "tutorialStepTitle"
                        Layout.fillWidth: true
                        text: page.currentStep.title || ""
                        textFormat: Text.PlainText
                        font.pixelSize: UiMetrics.textPageTitle
                        font.bold: true
                        wrapMode: Text.Wrap
                        color: page.theme.text
                    }
                    Label {
                        Layout.fillWidth: true
                        text: page.currentStep.summary || ""
                        textFormat: Text.PlainText
                        font.pixelSize: UiMetrics.textEmphasis
                        wrapMode: Text.Wrap
                        color: page.theme.muted
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: instructionList.implicitHeight + UiMetrics.spaceSection * 2
                    color: page.theme.surface
                    radius: UiMetrics.radiusCard
                    ColumnLayout {
                        id: instructionList
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: UiMetrics.spaceSection
                        spacing: UiMetrics.settingsSectionSpacing
                        Repeater {
                            model: page.currentStep.instructions || []
                            delegate: RowLayout {
                                required property int index
                                required property string modelData
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignTop
                                spacing: UiMetrics.spaceGroup
                                Label {
                                    text: (index + 1).toString()
                                    Layout.alignment: Qt.AlignTop
                                    font.pixelSize: UiMetrics.textEmphasis
                                    font.bold: true
                                    color: page.theme.accent
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData
                                    textFormat: Text.PlainText
                                    wrapMode: Text.Wrap
                                    color: page.theme.text
                                }
                            }
                        }
                    }
                }
                AppButton {
                    objectName: "tutorialDetailsButton"
                    visible: (page.currentStep.note || "").length > 0
                    text: page.detailsExpanded ? qsTr("참고사항 접기") : qsTr("참고사항 보기")
                    appearance: AppButton.Soft
                    dark: page.dark
                    onClicked: page.detailsExpanded = !page.detailsExpanded
                }
                Label {
                    Layout.fillWidth: true
                    visible: page.detailsExpanded && text.length > 0
                    text: page.currentStep.note || ""
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    color: page.theme.muted
                }
            }
        }
        AppComboBox {
            objectName: "tutorialContents"
            Layout.fillWidth: true
            model: page.steps
            textRole: "title"
            currentIndex: page.stepIndex
            Accessible.name: qsTr("튜토리얼 목차")
            onActivated: function(index) { page.goTo(index) }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.controller.message
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            color: page.theme.danger
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceControl
            AppButton {
                objectName: "tutorialSkipButton"
                text: page.controller.reviewing ? qsTr("닫기") : qsTr("건너뛰기")
                dark: page.dark
                onClicked: page.controller.skip()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "tutorialPreviousButton"
                text: qsTr("이전")
                enabled: page.stepIndex > 0
                dark: page.dark
                onClicked: page.goTo(page.stepIndex - 1)
            }
            AppButton {
                objectName: "tutorialNextButton"
                text: page.lastStep ? qsTr("완료") : qsTr("다음")
                primary: true
                dark: page.dark
                onClicked: {
                    if (page.lastStep) page.controller.finish()
                    else page.goTo(page.stepIndex + 1)
                }
            }
        }
    }
}

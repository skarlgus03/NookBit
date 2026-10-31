import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

// 첫 실행 입력만 초안으로 보관하고 적용·건너뛰기는 전용 창구에 요청하는 화면.
PageSurface {
    id: page
    objectName: "initialSetupPage"
    required property var controller
    property url selectedRules: ""
    property bool initialized: false
    Component.onCompleted: initialized = true
    onVisibleChanged: if (initialized && !visible) { folderPicker.close(); rulePicker.close() }
    background: Rectangle { color: page.theme.window }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        spacing: UiMetrics.spaceSection
        Label {
            Layout.fillWidth: true
            text: qsTr("NookBit을 시작할 준비")
            font.pixelSize: UiMetrics.textSectionTitle
            font.bold: true
            color: page.theme.text
        }
        Label {
            Layout.fillWidth: true
            text: page.controller.existingConfiguration
                ? qsTr("기존 설정을 채워 두었어요. 그대로 시작하거나 필요한 것만 바꿔 주세요.")
                : qsTr("처음 한 번만 선택하면 돼요. 나중에도 각 설정 화면에서 바꿀 수 있어요.")
            wrapMode: Text.Wrap
            color: page.theme.muted
        }
        ScrollView {
            id: choices
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: choices.availableWidth
                spacing: UiMetrics.spaceSection
                SettingsSection {
                    Layout.fillWidth: true
                    visible: page.controller.organizerAvailable
                    title: qsTr("기본 정리 폴더")
                    dark: page.dark
                    Label { Layout.fillWidth: true; text: qsTr("분류 규칙에서 기본 폴더를 기준으로 정리할 위치예요."); wrapMode: Text.Wrap; color: page.theme.muted }
                    RowLayout {
                        Layout.fillWidth: true
                        AppTextField {
                            id: destination
                            objectName: "setupDestination"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            text: page.controller.destinationRoot
                            placeholderText: qsTr("정리 폴더의 전체 경로")
                            Accessible.name: qsTr("기본 정리 폴더")
                        }
                        AppButton {
                            text: qsTr("폴더 선택")
                            dark: page.dark
                            // 입력 중에는 경로를 조회하지 않고 선택창을 열 때만 시작 폴더를 구하는 함수.
                            onClicked: {
                                folderPicker.currentFolder = page.controller.folderUrl(destination.text)
                                folderPicker.open()
                            }
                        }
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    visible: page.controller.organizerAvailable
                    title: qsTr("분류 규칙")
                    dark: page.dark
                    AppComboBox {
                        id: ruleChoice
                        objectName: "setupRuleChoice"
                        Layout.fillWidth: true
                        model: [page.controller.existingConfiguration ? qsTr("현재 규칙 유지") : qsTr("기본 규칙 사용"), qsTr("JSON 규칙 불러오기")]
                        Accessible.name: qsTr("시작할 분류 규칙")
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: ruleChoice.currentIndex === 0
                        text: page.controller.ruleLabel
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: page.theme.muted
                    }
                    AppButton {
                        visible: ruleChoice.currentIndex === 1
                        text: qsTr("JSON 파일 선택")
                        dark: page.dark
                        onClicked: rulePicker.open()
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: ruleChoice.currentIndex === 1
                        text: page.selectedRules.toString().length ? page.controller.folderPath(page.selectedRules) : qsTr("선택한 파일이 없어요.")
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: page.theme.muted
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: ruleChoice.currentIndex === 1
                        text: qsTr("시작하기를 누르면 검증한 뒤 적용해요. 외부 목적지는 자동으로 허용하지 않아요.")
                        wrapMode: Text.Wrap
                        color: page.theme.muted
                    }
                }
                SettingsSection {
                    Layout.fillWidth: true
                    visible: page.controller.companionAvailable
                    title: qsTr("컴패니언")
                    dark: page.dark
                    AppSwitch {
                        id: companionShown
                        objectName: "setupCompanionSwitch"
                        Layout.fillWidth: true
                        text: qsTr("바탕화면에 컴패니언 표시")
                        checked: page.controller.companionShown
                        dark: page.dark
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: !page.controller.organizerAvailable || !page.controller.companionAvailable
                    text: qsTr("꺼져 있는 기능의 설정은 생략했어요. 기능은 모듈 관리에서 켤 수 있어요.")
                    wrapMode: Text.Wrap
                    color: page.theme.muted
                }
            }
        }
        Label {
            objectName: "setupError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.controller.message
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
            color: page.theme.danger
            Accessible.role: Accessible.AlertMessage
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("여기서는 설정만 저장해요. 파일 이동은 정리하기에서 확인 후 시작돼요.")
            wrapMode: Text.Wrap
            color: page.theme.muted
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceControl
            LargeActionButton {
                objectName: "setupSkipButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("건너뛰기")
                dark: page.dark
                onClicked: page.controller.skip()
            }
            LargeActionButton {
                objectName: "setupFinishButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("시작하기")
                primary: true
                dark: page.dark
                onClicked: page.controller.finish(destination.text, ruleChoice.currentIndex === 1, page.selectedRules, companionShown.checked)
            }
        }
    }
    NativeDialogs.FolderDialog {
        id: folderPicker
        title: qsTr("기본 정리 폴더 선택")
        onAccepted: destination.text = page.controller.folderPath(selectedFolder)
    }
    NativeDialogs.FileDialog {
        id: rulePicker
        title: qsTr("분류 규칙 JSON 선택")
        nameFilters: [qsTr("JSON 파일 (*.json)")]
        fileMode: NativeDialogs.FileDialog.OpenFile
        onAccepted: page.selectedRules = selectedFile
    }
}

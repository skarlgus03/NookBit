import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 실행 중인 기능은 유지하고 다음 시작에 사용할 선택만 편집하는 호스트 페이지.
Item {
    id: page
    objectName: "moduleManagementPage"
    required property var controller
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        Label {
            Layout.fillWidth: true
            text: qsTr("필요한 기능만 켜 두세요")
            font.pixelSize: UiMetrics.textSectionTitle
            font.bold: true
            color: page.theme.text
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("선택을 저장한 뒤 앱을 완전히 종료했다 다시 켜면 적용돼요. 규칙·캐릭터 팩·작업 기록은 지워지지 않아요.")
            font.pixelSize: UiMetrics.textBody
            color: page.theme.muted
            wrapMode: Text.Wrap
        }
        Label {
            objectName: "moduleSessionOverride"
            Layout.fillWidth: true
            visible: page.controller.sessionOverride
            text: qsTr("이번 실행에는 시작 옵션이 적용되어 있어요. 저장된 선택은 다음에 시작 옵션 없이 실행할 때 적용돼요.")
            font.pixelSize: UiMetrics.textBody
            color: page.theme.warning
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: moduleScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: moduleScroll.availableWidth
                spacing: UiMetrics.spaceGroup
                Repeater {
                    model: page.controller.modules
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: rowContents.implicitHeight + UiMetrics.spaceSection * 2
                        color: page.theme.surface
                        radius: UiMetrics.radiusCard
                        ColumnLayout {
                            id: rowContents
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.margins: UiMetrics.spaceSection
                            spacing: UiMetrics.spaceSmall
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    font.pixelSize: UiMetrics.textEmphasis
                                    font.bold: true
                                    color: page.theme.text
                                }
                                AppSwitch {
                                    objectName: "moduleToggle_" + row.modelData.id
                                    dark: page.dark
                                    checked: row.modelData.requested
                                    Accessible.name: row.modelData.title + qsTr(" 다음 실행에 사용")
                                    onToggled: page.controller.setEnabled(row.modelData.id, checked)
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: row.modelData.description
                                font.pixelSize: UiMetrics.textBody
                                color: page.theme.muted
                                wrapMode: Text.Wrap
                            }
                            Label {
                                Layout.fillWidth: true
                                text: (row.modelData.active ? qsTr("현재 켜짐") : qsTr("현재 꺼짐")) + " · "
                                    + (row.modelData.effective ? qsTr("다음 실행: 켜짐") : qsTr("다음 실행: 꺼짐"))
                                font.pixelSize: UiMetrics.textBody
                                color: page.theme.muted
                                wrapMode: Text.Wrap
                            }
                            Label {
                                objectName: "moduleReason_" + row.modelData.id
                                Layout.fillWidth: true
                                visible: row.modelData.reason.length > 0
                                text: row.modelData.reason
                                font.pixelSize: UiMetrics.textBody
                                color: page.theme.warning
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }
            }
        }
        Label {
            objectName: "moduleSaveStatus"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.controller.status.length > 0 ? page.controller.status
                : page.controller.dirty ? qsTr("저장하지 않은 변경이 있어요.")
                : page.controller.restartRequired ? qsTr("저장된 선택을 적용하려면 앱을 완전히 종료한 뒤 다시 실행해 주세요.") : ""
            font.pixelSize: UiMetrics.textBody
            color: page.theme.muted
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "moduleDiscardButton"
                text: qsTr("변경 취소")
                dark: page.dark
                enabled: page.controller.dirty
                onClicked: page.controller.discard()
            }
            AppButton {
                objectName: "moduleSaveButton"
                text: qsTr("저장")
                primary: true
                dark: page.dark
                enabled: page.controller.canSave
                onClicked: page.controller.save()
            }
        }
    }
}

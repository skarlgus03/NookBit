import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 920
    height: 760
    minimumWidth: 680
    minimumHeight: 620
    visible: true
    title: qsTr("NookBit")
    color: "#f5f5f2"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 32
        spacing: 14

        Label {
            text: qsTr("NookBit")
            font.pixelSize: 30
            font.bold: true
            color: "#232323"
        }

        Label {
            text: qsTr("정리할 파일을 먼저 대기 목록에 담아 주세요.")
            font.pixelSize: 16
            color: "#5a5a56"
        }

        Label {
            Layout.fillWidth: true
            visible: previewController.recoveryNotice.length > 0
            text: previewController.recoveryNotice
            color: "#9b5c23"
            wrapMode: Text.WordWrap
        }

        Button {
            visible: previewController.recoveryCases.length > 0
            text: qsTr("중단 작업 확인")
            onClicked: {
                previewController.refreshRecoveryCases()
                recoveryDialog.open()
            }
        }

        Rectangle {
            id: dropZone
            property bool dragging: false
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            radius: 18
            color: dragging ? "#e8f4e5" : "#ffffff"
            border.width: dragging ? 2 : 1
            border.color: dragging ? "#4d8f57" : "#d8dcd5"

            Column {
                anchors.centerIn: parent
                spacing: 8

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("파일을 여기로 끌어놓으세요")
                    font.pixelSize: 19
                    font.bold: true
                    color: "#24382a"
                }
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("원본 파일은 이동하거나 복사하지 않습니다.")
                    color: "#657065"
                }
            }

            DropArea {
                anchors.fill: parent
                onEntered: (drag) => {
                    const canRegister = drag.hasUrls && (drag.supportedActions & Qt.CopyAction)
                    dropZone.dragging = canRegister
                    if (canRegister)
                        drag.accept(Qt.CopyAction)
                    else
                        drag.accepted = false
                }
                onExited: dropZone.dragging = false
                onDropped: (drop) => {
                    dropZone.dragging = false
                    if (drop.hasUrls && (drop.supportedActions & Qt.CopyAction)) {
                        for (const url of drop.urls)
                            pendingFiles.addUrl(url)
                        drop.accept(Qt.CopyAction)
                    } else {
                        drop.accepted = false
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: qsTr("파일정리대기")
                font.pixelSize: 19
                font.bold: true
                color: "#232323"
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("%1개 대기 중").arg(pendingFiles.count)
                color: "#5a5a56"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            radius: 16
            color: "#ffffff"
            border.color: "#deded8"

            Label {
                anchors.centerIn: parent
                visible: pendingFiles.count === 0
                text: qsTr("아직 대기 중인 파일이 없어요.")
                color: "#7b817a"
            }

            ListView {
                id: pendingList
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                spacing: 4
                visible: pendingFiles.count > 0
                model: pendingFiles

                delegate: Rectangle {
                    required property int index
                    required property string fileName
                    required property string fullPath
                    width: pendingList.width
                    height: 70
                    radius: 10
                    color: index % 2 === 0 ? "#f7f9f6" : "#ffffff"

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Label {
                                text: fileName
                                font.bold: true
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                            Label {
                                text: fullPath
                                color: "#67716a"
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }

                        Button {
                            text: qsTr("목록에서 빼기")
                            onClicked: pendingFiles.removeAt(index)
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: qsTr("정리할 위치")
                color: "#232323"
            }
            TextField {
                id: destinationField
                Layout.fillWidth: true
                text: previewController.defaultDestinationRoot
                placeholderText: qsTr("정리할 폴더의 전체 경로")
                onTextEdited: previewController.clear()
            }
            Button {
                text: qsTr("미리보기 만들기")
                enabled: pendingFiles.count > 0
                onClicked: previewController.createPreview(destinationField.text)
            }
            Button {
                text: previewController.hasFrozenPlan ? qsTr("저장됨") : qsTr("계획 고정·저장")
                enabled: previewController.hasPreview && !previewController.hasFrozenPlan
                         && previewController.steps.length > 0
                onClicked: previewController.freezePreview()
            }
            Button {
                text: qsTr("저장 계획 검사")
                enabled: previewController.hasFrozenPlan && !previewController.hasApprovedPlan
                onClicked: previewController.verifySavedPlan()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Button {
                text: qsTr("파일 하나 승인")
                enabled: previewController.hasFrozenPlan && !previewController.hasApprovedPlan
                         && pendingFiles.count === 1 && previewController.steps.length === 1
                         && previewController.steps[0].status === qsTr("예정")
                onClicked: previewController.approvePlan()
            }
            Button {
                text: qsTr("승인한 파일 이동")
                enabled: previewController.hasApprovedPlan && pendingFiles.count === 1
                onClicked: previewController.moveApprovedFile()
            }
            Button {
                text: qsTr("마지막 이동 되돌리기")
                enabled: previewController.canUndo
                onClicked: previewController.undoLastMove()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: qsTr("한 번에 파일 하나만 이동할 수 있어요.")
                color: "#67716a"
            }
        }

        Label {
            Layout.fillWidth: true
            text: previewController.message.length > 0
                  ? previewController.message
                  : qsTr("기본 규칙: 사진 → Pictures/연도/월, PDF → Documents/PDF")
            color: "#426349"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 16
            color: "#ffffff"
            border.color: "#deded8"

            Label {
                anchors.centerIn: parent
                visible: !previewController.hasPreview
                text: qsTr("미리보기를 만들면 예상 목적지가 여기에 표시됩니다.")
                color: "#7b817a"
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                visible: previewController.hasPreview
                spacing: 6

                ListView {
                    id: previewList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: previewController.steps
                    delegate: ColumnLayout {
                        required property var modelData
                        width: previewList.width
                        spacing: 2
                        Label {
                            Layout.fillWidth: true
                            text: modelData.fileName + "  ·  " + modelData.status + "  ·  " + modelData.rule
                            font.bold: true
                            elide: Text.ElideMiddle
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.destination
                            color: "#67716a"
                            elide: Text.ElideMiddle
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: previewController.steps.length === 0
                    text: qsTr("이동 대상으로 분류된 파일이 없어요.")
                    color: "#7b817a"
                }
                Label {
                    Layout.fillWidth: true
                    visible: previewController.warnings.length > 0
                    text: qsTr("경고: ") + previewController.warnings.join("\n")
                    color: "#9b5c23"
                    wrapMode: Text.WordWrap
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: pendingFiles.lastMessage.length > 0
                  ? pendingFiles.lastMessage
                  : qsTr("승인 전에는 파일을 이동하지 않습니다.")
            color: "#426349"
            wrapMode: Text.WordWrap
        }
    }

    Dialog {
        id: recoveryDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 760)
        height: Math.min(root.height - 32, 560)
        modal: true
        title: qsTr("중단 작업 확인")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("파일을 자동으로 옮기거나 삭제하지 않습니다. 원본과 목적지를 직접 확인해 주세요.")
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("파일 상태 다시 검사")
                onClicked: previewController.refreshRecoveryCases()
            }
            Label {
                Layout.fillWidth: true
                text: previewController.recoveryMessage
                wrapMode: Text.WordWrap
                color: "#9b5c23"
            }
            ScrollView {
                id: recoveryScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Column {
                    width: recoveryScroll.availableWidth
                    spacing: 8

                    Repeater {
                        model: previewController.recoveryCases
                        delegate: Frame {
                            required property var modelData
                            width: parent.width

                            ColumnLayout {
                                width: parent.width
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("작업 기록: ") + modelData.planId
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("원본 (") + modelData.sourceState + "): " + modelData.source
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("목적지 (") + modelData.destinationState + "): " + modelData.destination
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    wrapMode: Text.WordWrap
                                    color: "#9b5c23"
                                }
                                Button {
                                    text: qsTr("현재 상태로 기록 확정")
                                    enabled: modelData.canResolve
                                    onClicked: previewController.resolveRecoveryCase(modelData.planId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

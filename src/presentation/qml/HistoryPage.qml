import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 작업 기록 조회와 해당 작업의 되돌리기 확인창을 소유하는 페이지.
PageSurface {
    id: page
    required property var controller
    required property Item overlayHost
    property bool busy: false
    property bool initialized: false
    Component.onCompleted: initialized = true
    onVisibleChanged: if (initialized && !visible)
                          undoHistoryDialog.close()

    objectName: "historyDialog"

    ColumnLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceCompact

        Label {
            Layout.fillWidth: true
            text: page.controller.historyMessage
            wrapMode: Text.WordWrap
            color: page.theme.success
        }
        AppButton {
            text: qsTr("목록 새로고침")
            onClicked: page.controller.refreshHistory()
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: UiMetrics.spaceGroup

            ListView {
                id: historyList
                objectName: "historyList"
                Layout.preferredWidth: UiMetrics.historyListWidth
                Layout.fillHeight: true
                clip: true
                spacing: UiMetrics.spaceSmall
                model: page.controller.historyEntries
                delegate: Column {
                    required property var modelData
                    width: historyList.width
                    spacing: UiMetrics.spaceControl
                    Label {
                        objectName: "historyDateHeading"
                        visible: modelData.startsDateGroup
                        height: visible ? implicitHeight : 0
                        text: modelData.dateGroup
                        color: page.theme.text
                        font.pixelSize: UiMetrics.textSectionTitle
                        font.weight: UiMetrics.groupTitleWeight
                    }
                    Rectangle {
                        width: parent.width
                        height: UiMetrics.historyRowHeight
                        radius: UiMetrics.radiusControl
                        color: modelData.planId === page.controller.selectedHistoryPlanId ? page.theme.menuSelected :
                                                                                            page.theme.alternate
                        border.color: page.theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: UiMetrics.spaceCompact
                            spacing: UiMetrics.spaceTiny
                            Label {
                                Layout.fillWidth: true
                                text: modelData.time + " · " + modelData.status
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: qsTr("대상 %1개 · 현재 이동됨 %2개").arg(modelData.fileCount).arg(modelData.appliedCount)
                                color: page.theme.muted
                            }
                            AppButton {
                                text: qsTr("파일 보기")
                                onClicked: page.controller.selectHistoryPlan(modelData.planId)
                            }
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: UiMetrics.spaceControl

                Label {
                    Layout.fillWidth: true
                    text: page.controller.selectedHistoryPlanId.length > 0 ? qsTr("선택한 작업의 파일") : qsTr(
                                                                                 "왼쪽에서 작업을 선택해 주세요.")
                    font.bold: true
                }
                ScrollView {
                    id: historyStepScroll
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    Column {
                        width: historyStepScroll.availableWidth
                        spacing: UiMetrics.spaceSmall

                        Repeater {
                            model: page.controller.historySteps
                            delegate: Frame {
                                required property var modelData
                                width: parent.width

                                ColumnLayout {
                                    width: parent.width
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.fileName + " · " + modelData.status
                                        font.bold: true
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("원본: ") + modelData.source
                                        wrapMode: Text.WrapAnywhere
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: qsTr("목적지: ") + modelData.destination
                                        wrapMode: Text.WrapAnywhere
                                    }
                                }
                            }
                        }
                    }
                }
                AppButton {
                    text: qsTr("이 작업 되돌리기")
                    enabled: page.controller.canUndoSelectedHistory
                    onClicked: undoHistoryDialog.open()
                }
            }
        }
    }

    Dialog {
        id: undoHistoryDialog
        objectName: "historyUndoConfirmation"
        enabled: !page.busy
        parent: page.overlayHost
        anchors.centerIn: parent
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        modal: true
        title: qsTr("선택한 작업 되돌리기")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: page.controller.undoSelectedHistory()

        Label {
            width: parent.width
            text: qsTr("이 작업에서 이동된 파일 %1개를 원래 위치로 되돌릴까요?\n" + "파일이 바뀌었거나 원본 경로가 사용 중이면 중단합니다.").arg(
                      page.controller.selectedHistoryFileCount)
            wrapMode: Text.WordWrap
        }
    }
}

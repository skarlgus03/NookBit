import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 중단 작업 목록과 명시적 실행 확인창을 소유하는 페이지.
PageSurface {
    id: page
    required property var controller
    required property Item overlayHost
    property bool busy: false
    property bool inspecting: false
    property bool initialized: false
    Component.onCompleted: initialized = true
    // 페이지를 떠나면 승인 대기를 취소하고 보조 확인창도 닫는 함수.
    onVisibleChanged: if (initialized && !visible) {
                          if (controller.awaitingRecoveryConfirmation)
                              controller.cancelRecoveryChoice();
                          recoveryConfirmDialog.close();
                      }

    objectName: "recoveryDialog"

    ColumnLayout {
        anchors.fill: parent
        spacing: UiMetrics.spaceCompact

        Label {
            Layout.fillWidth: true
            text: qsTr("시작 검사와 '나중에'는 파일을 변경하지 않습니다. 재개·되돌리기는 직접 선택하고 확인해야 실행됩니다.")
            wrapMode: Text.WordWrap
        }
        AppButton {
            text: qsTr("파일 상태 다시 검사")
            enabled: !page.controller.recoveryBusy
            onClicked: page.controller.refreshRecoveryCases()
        }
        Label {
            Layout.fillWidth: true
            text: page.controller.recoveryMessage
            visible: !page.inspecting
            wrapMode: Text.WordWrap
            color: page.theme.warning
        }
        RowLayout {
            objectName: "recoveryInspectionIndicator"
            Layout.fillWidth: true
            visible: page.inspecting
            spacing: UiMetrics.spaceControl
            BusyIndicator {
                running: page.inspecting
                Layout.preferredWidth: UiMetrics.iconSize
                Layout.preferredHeight: UiMetrics.iconSize
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("중단 작업 목록 확인 중… 파일은 변경하지 않아요.")
                color: page.theme.muted
                wrapMode: Text.WordWrap
            }
        }
        ScrollView {
            id: recoveryScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Column {
                width: recoveryScroll.availableWidth
                spacing: UiMetrics.spaceControl

                Repeater {
                    model: page.controller.recoveryCases
                    delegate: Frame {
                        id: recoveryCaseFrame
                        required property var modelData
                        width: parent.width

                        ColumnLayout {
                            width: parent.width
                            Label {
                                Layout.fillWidth: true
                                text: qsTr("작업 기록: ") + modelData.planId + (modelData.whole ? "" : qsTr(" · 파일 ")
                                                                                              + modelData.partIndex
                                                                                              + "/" + modelData.partCount)
                                font.bold: true
                                elide: Text.ElideMiddle
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: !!modelData.whole
                                text: qsTr("이동 완료 %1 · 미시작 %2 · 복원 완료 %3\n확인 필요 %4 · 보존 임시 파일 %5").arg(modelData.moved
                                                                                                       || 0).arg(
                                          modelData.pending || 0).arg(modelData.restored || 0).arg(
                                          modelData.needsAttention || 0).arg(modelData.preserved || 0)
                                wrapMode: Text.WordWrap
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: !modelData.whole
                                text: qsTr("원본 (") + modelData.sourceState + "): " + modelData.source
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                Layout.fillWidth: true
                                visible: !modelData.whole
                                text: qsTr("목적지 (") + modelData.destinationState + "): " + modelData.destination
                                wrapMode: Text.WrapAnywhere
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.detail
                                wrapMode: Text.WordWrap
                                color: page.theme.warning
                            }
                            CheckBox {
                                id: recoveryPathsToggle
                                visible: !!modelData.whole
                                text: qsTr("파일 경로 보기")
                            }
                            Repeater {
                                model: recoveryPathsToggle.checked && recoveryCaseFrame.modelData.whole
                                       ? recoveryCaseFrame.modelData.files : []
                                delegate: Label {
                                    required property var modelData
                                    Layout.fillWidth: true
                                    text: "[" + modelData.state + qsTr("] 원본: ") + modelData.source + qsTr("\n목적지: ")
                                          + modelData.destination + (modelData.staging ? qsTr("\n임시 경로: ")
                                                                                         + modelData.staging : "")
                                    textFormat: Text.PlainText
                                    wrapMode: Text.WrapAnywhere
                                }
                            }
                            RowLayout {
                                visible: !!modelData.whole
                                AppButton {
                                    text: qsTr("재개")
                                    enabled: recoveryCaseFrame.modelData.canResume && !page.controller.recoveryBusy
                                    onClicked: page.controller.requestRecoveryChoice(recoveryCaseFrame.modelData.planId,
                                                                                     "resume")
                                }
                                AppButton {
                                    text: qsTr("되돌리기")
                                    enabled: recoveryCaseFrame.modelData.canRollback && !page.controller.recoveryBusy
                                    onClicked: page.controller.requestRecoveryChoice(recoveryCaseFrame.modelData.planId,
                                                                                     "rollback")
                                }
                                AppButton {
                                    text: qsTr("나중에")
                                    enabled: !page.controller.recoveryBusy
                                    onClicked: {
                                        page.controller.requestRecoveryChoice(recoveryCaseFrame.modelData.planId,
                                                                              "later");
                                        page.close();
                                    }
                                }
                            }
                            AppButton {
                                text: qsTr("현재 상태로 기록 확정")
                                visible: modelData.partIndex === 1 && (!modelData.whole || modelData.canResolve)
                                enabled: modelData.canResolve && !page.controller.recoveryBusy
                                onClicked: page.controller.resolveRecoveryCase(modelData.planId)
                            }
                        }
                    }
                }
            }
        }
    }

    Connections {
        target: page.controller
        // Controller의 승인 대기 상태에 맞춰 확인창만 열거나 닫는 함수.
        function onRecoveryConfirmationChanged() {
            if (page.controller.awaitingRecoveryConfirmation)
                recoveryConfirmDialog.open();
            else
                recoveryConfirmDialog.close();
        }
    }
    Dialog {
        id: recoveryConfirmDialog
        enabled: !page.busy
        objectName: "recoveryConfirmDialog"
        parent: page.overlayHost
        anchors.centerIn: parent
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        modal: true
        title: qsTr("중단 작업 실행 확인")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: page.controller.confirmRecoveryChoice()
        onRejected: page.controller.cancelRecoveryChoice()
        Label {
            width: parent.width
            text: page.controller.recoveryConfirmationText
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }
    }
}

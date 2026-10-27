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

    readonly property var selectedEntry: controller.historyEntries.find(entry => entry.planId === controller.selectedHistoryPlanId) || null
    background: Rectangle { color: page.theme.window }
    RowLayout {
        anchors.fill: parent
        spacing: 0
        HistoryTimeline {
            Layout.preferredWidth: UiMetrics.historyListWidth
            Layout.fillHeight: true
            entries: page.controller.historyEntries
            selectedPlanId: page.controller.selectedHistoryPlanId
            busy: page.busy
            dark: page.dark
            onPlanRequested: planId => page.controller.selectHistoryPlan(planId)
            onRefreshRequested: page.controller.refreshHistory()
        }
        Rectangle {
            Layout.preferredWidth: UiMetrics.borderNormal
            Layout.fillHeight: true
            color: page.theme.border
        }
        HistoryDetail {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.fillHeight: true
            entry: page.selectedEntry
            steps: page.controller.historySteps
            message: page.controller.historyMessage
            canUndo: page.controller.canUndoSelectedHistory
            undoCount: page.controller.selectedHistoryFileCount
            busy: page.busy
            dark: page.dark
            onUndoRequested: undoHistoryDialog.open()
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

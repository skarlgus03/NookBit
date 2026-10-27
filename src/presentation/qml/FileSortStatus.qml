import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 수동 파일 정리의 진행·완료 표시만 담당하며 작업을 생성하지 않는 화면.
Rectangle {
    id: status
    required property var controller
    required property var progress
    property bool completed: false
    property bool success: false
    property string resultMessage: ""
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal backRequested()
    color: theme.surface
    BackButton {
        objectName: status.completed ? "fileCompleteBackButton" : "fileProgressBackButton"
        visible: status.completed
        anchors.left: parent.left; anchors.top: parent.top
        anchors.margins: UiMetrics.pageInset
        onClicked: status.backRequested()
    }
    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top; anchors.bottom: parent.bottom
        anchors.margins: UiMetrics.pageInset
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        spacing: UiMetrics.spaceGroup
        Item { Layout.fillHeight: true }
        NavigationIcon { visible: !status.completed; Layout.alignment: Qt.AlignHCenter; Layout.preferredWidth: UiMetrics.filePreviewSize; Layout.preferredHeight: UiMetrics.filePreviewSize; kind: "folder"; strokeColor: status.dark ? status.theme.text : status.theme.accent }
        Label {
            Layout.fillWidth: true
            text: status.completed ? (status.success ? qsTr("정리를 마쳤어요") : qsTr("정리를 완료하지 못했어요")) : qsTr("파일을 정리하고 있어요")
            font.pixelSize: UiMetrics.textSectionTitle
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
        }
        Label { Layout.fillWidth: true; visible: !status.completed; text: status.progress.stageLabel; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        Label { Layout.fillWidth: true; visible: !status.completed; text: status.progress.currentFileName; font.pixelSize: UiMetrics.textEmphasis; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideMiddle }
        Label { Layout.fillWidth: true; visible: !status.completed; text: status.progress.filePosition; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        ProgressBar {
            Layout.fillWidth: true
            visible: !status.completed
            from: 0; to: 1
            value: status.progress.byteFraction
            indeterminate: !status.progress.byteProgressKnown
        }
        Label { visible: !status.completed && status.progress.byteProgressKnown; text: status.progress.byteProgressText }
        Label { Layout.fillWidth: true; visible: status.completed; text: status.resultMessage; wrapMode: Text.Wrap }
        Label { Layout.fillWidth: true; visible: status.completed && !status.success; text: qsTr("이동하지 못한 파일은 대기 목록과 작업 기록에서 확인해 주세요."); wrapMode: Text.Wrap }
        AppButton {
            visible: status.completed && status.success
            text: qsTr("되돌리기")
            enabled: status.controller.canUndo && !status.progress.busy
            // 되돌리기를 요청한 뒤 대기 목록에서 기존 되돌리기 결과 안내를 확인하는 함수.
            onClicked: { status.controller.undoLastMove(); status.backRequested() }
        }
        Item { Layout.fillHeight: true }
    }
}

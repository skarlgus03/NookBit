import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 고정된 이동 계획을 같은 파일 정리 창에서 확인하며 승인 없이 작업을 시작하지 않는 화면.
Rectangle {
    id: confirmation
    objectName: "fileSortConfirmation"
    required property var controller
    property bool dark: false
    signal cancelRequested()
    signal confirmRequested()
    color: dark ? "#161a22" : "#ffffff"
    BackButton { objectName: "fileConfirmationBackButton"; anchors.left: parent.left; anchors.top: parent.top; anchors.margins: UiMetrics.pageInset; onClicked: confirmation.cancelRequested() }
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        height: Math.min(parent.height - UiMetrics.controlHeight - UiMetrics.pageInset * 4, UiMetrics.confirmationContentHeight)
        spacing: UiMetrics.spaceGroup
        Label { Layout.fillWidth: true; text: qsTr("파일 이동 최종 확인"); font.pixelSize: UiMetrics.textSectionTitle; font.bold: true; horizontalAlignment: Text.AlignHCenter }
        Label { Layout.fillWidth: true; text: qsTr("아래 %1개 파일을 이동할까요?").arg(confirmation.controller.confirmationSteps.length); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        Label { Layout.fillWidth: true; text: qsTr("미분류·제외·건너뜀 파일은 그대로 둡니다."); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap }
        ListView {
            id: files
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            spacing: UiMetrics.spaceGroup
            model: confirmation.controller.confirmationSteps
            ScrollBar.vertical: ScrollBar {}
            delegate: Column {
                required property var modelData
                width: files.width
                spacing: UiMetrics.spaceExtraSmall
                Label { width: parent.width; text: modelData.source; wrapMode: Text.WrapAnywhere }
                Label { width: parent.width; text: qsTr("→ %1").arg(modelData.destination); wrapMode: Text.WrapAnywhere; font.bold: true }
            }
        }
        Label { Layout.fillWidth: true; text: qsTr("확인 후에도 파일 상태를 다시 검사해요. 변경이 있으면 이동하지 않습니다."); wrapMode: Text.Wrap }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceGroup
            AppButton { Layout.fillWidth: true; text: qsTr("취소"); onClicked: confirmation.cancelRequested() }
            AppButton {
                objectName: "fileConfirmationExecuteButton"
                Layout.fillWidth: true
                implicitHeight: UiMetrics.largeActionHeight
                text: qsTr("확인하고 이동")
                enabled: confirmation.controller.awaitingConfirmation
                onClicked: confirmation.confirmRequested()
            }
        }
    }
}

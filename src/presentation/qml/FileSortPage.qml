import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

// 파일 대기 목록·정리 위치·정리 요청은 홈과 분리된 페이지에서만 표시한다.
Rectangle {
    id: home
    objectName: "fileSortPage"
    property bool dark: false
    signal pageRequested(int page)
    signal recoveryRequested()
    readonly property color foreground: dark ? "#edf2f8" : "#273344"
    readonly property color muted: dark ? "#aab6c7" : "#748091"
    readonly property color surface: dark ? "#242d3b" : "#ffffff"
    readonly property color edge: dark ? "#3d4757" : "#e1e6ed"
    color: dark ? "#161a22" : "#ffffff"
    radius: UiMetrics.radiusControl
    border.color: edge

    component FlatButton: T.Button {
        id: control
        padding: UiMetrics.buttonPadding
        implicitWidth: Math.max(UiMetrics.buttonMinWidth, contentItem.implicitWidth + leftPadding + rightPadding)
        implicitHeight: UiMetrics.controlHeight
        contentItem: Text {
            text: control.text
            color: control.enabled ? home.foreground : home.muted
            font.pixelSize: UiMetrics.textBody
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: UiMetrics.radiusControl
            color: home.surface
            border.color: control.hovered || control.visualFocus ? "#7898bb" : home.edge
            border.width: control.hovered || control.visualFocus ? 2 : 1
            opacity: control.enabled ? 1 : 0.6
        }
    }
    ScrollView {
        id: homeScroll
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: homeScroll.availableWidth
            spacing: UiMetrics.spaceControl
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 92
                radius: 14
                color: drop.containsDrag ? (home.dark ? "#304457" : "#eaf1f9") : home.surface
                border.color: drop.containsDrag ? "#7898bb" : home.edge
                border.width: drop.containsDrag ? 2 : 1
                Column {
                    anchors.centerIn: parent
                    spacing: UiMetrics.spaceSmall
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("＋ 파일 끌어놓기"); font.bold: true; font.pixelSize: UiMetrics.textEmphasis; color: home.foreground }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("아직 원본 파일은 이동하지 않아요"); font.pixelSize: UiMetrics.textBody; color: home.muted }
                }
                DropArea {
                    id: drop
                    anchors.fill: parent
                    // 외부 드래그에는 복사 형식의 목록 등록만 허용하는 함수.
                    onEntered: function(drag) {
                        if (drag.hasUrls && (drag.supportedActions & Qt.CopyAction)) drag.accept(Qt.CopyAction)
                        else drag.accepted = false
                    }
                    // 파일을 이동하지 않고 대기 목록에 등록하는 함수.
                    onDropped: function(event) {
                        if (event.hasUrls && (event.supportedActions & Qt.CopyAction)) {
                            for (const url of event.urls) pendingFiles.addUrl(url)
                            event.accept(Qt.CopyAction)
                        } else event.accepted = false
                    }
                }
            }
            RowLayout {
                Label { text: qsTr("대기 목록"); color: home.foreground; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("%1개").arg(pendingFiles.count); color: home.muted }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(120, Math.min(170, home.height - 485))
                color: home.surface; radius: UiMetrics.radiusCard; border.color: home.edge
                Text { anchors.centerIn: parent; visible: pendingFiles.count === 0; text: qsTr("정리할 파일을 기다리고 있어요"); color: home.muted; font.pixelSize: UiMetrics.textBody }
                ListView {
                    id: files
                    anchors.fill: parent; anchors.margins: UiMetrics.spaceControl
                    model: pendingFiles; clip: true; spacing: UiMetrics.spaceExtraSmall
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Item {
                        required property int index
                        required property string fileName
                        required property string fullPath
                        required property bool available
                        width: files.width; height: 54
                        RowLayout {
                            anchors.fill: parent
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: UiMetrics.spaceTiny
                                Text { Layout.fillWidth: true; text: available ? fileName : qsTr("[없음] %1").arg(fileName); elide: Text.ElideMiddle; color: available ? home.foreground : "#bc6257"; font.pixelSize: UiMetrics.textBody }
                                Text { Layout.fillWidth: true; text: fullPath; elide: Text.ElideMiddle; color: home.muted; font.pixelSize: UiMetrics.textBody }
                            }
                            FlatButton { text: "×"; implicitWidth: UiMetrics.iconButtonSize; Accessible.name: qsTr("목록에서 빼기"); onClicked: pendingFiles.removeAt(index) }
                        }
                    }
                }
            }
            Label { Layout.fillWidth: true; visible: pendingFiles.storageNotice.length > 0; text: pendingFiles.storageNotice; wrapMode: Text.Wrap; color: "#bc6257"; font.pixelSize: UiMetrics.textBody }
            Label { text: qsTr("분류 규칙"); color: home.muted; font.pixelSize: UiMetrics.textBody }
            FlatButton {
                Layout.fillWidth: true
                text: previewController.activeRuleLabel + "  ›"
                contentItem: Text { text: parent.text; elide: Text.ElideMiddle; color: home.foreground; font.pixelSize: UiMetrics.textBody; verticalAlignment: Text.AlignVCenter }
                onClicked: home.pageRequested(1)
            }
            Label { text: qsTr("정리할 위치"); color: home.muted; font.pixelSize: UiMetrics.textBody }
            AppTextField {
                id: destination
                objectName: "compactDestinationField"
                Layout.fillWidth: true
                text: previewController.defaultDestinationRoot
                placeholderText: qsTr("정리 폴더의 전체 경로")
                font.pixelSize: UiMetrics.textBody
                color: home.foreground
                onTextEdited: previewController.clear()
            }
            RowLayout {
                Layout.fillWidth: true
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTr("분류 결과 보기")
                    enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0
                    onClicked: { previewController.createPreview(destination.text); home.pageRequested(2) }
                }
                FlatButton {
                    objectName: "compactUndoButton"
                    text: qsTr("되돌리기")
                    enabled: previewController.canUndo
                    onClicked: previewController.undoLastMove()
                }
            }
            FlatButton {
                objectName: "compactSortButton"
                Layout.fillWidth: true
                implicitHeight: UiMetrics.primaryActionHeight
                text: qsTr("정리하기")
                enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0 && !previewController.awaitingConfirmation
                contentItem: Text { text: parent.text; color: "#ffffff"; font.pixelSize: UiMetrics.textAction; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                background: Rectangle { radius: UiMetrics.radiusCard; color: parent.enabled ? "#405e80" : "#a7b4c4"; border.color: parent.hovered ? "#88a8c9" : color }
                // 새 분류 결과로 정리를 준비하되 실제 이동은 기존 최종 확인 이후에만 실행하는 함수.
                onClicked: {
                    previewController.createPreview(destination.text)
                    if (previewController.hasPreview && previewController.hasMovableSteps) previewController.prepareSort()
                }
            }
            Label { Layout.fillWidth: true; text: previewController.message; visible: text.length > 0; wrapMode: Text.Wrap; color: home.muted; font.pixelSize: UiMetrics.textBody }
            Label { Layout.fillWidth: true; visible: pendingFiles.unavailableCount > 0; text: qsTr("찾을 수 없는 파일을 목록에서 빼주세요."); wrapMode: Text.Wrap; color: "#bc6257"; font.pixelSize: UiMetrics.textBody }
            FlatButton { text: qsTr("중단 작업 확인"); visible: recoveryController.recoveryCases.length > 0; onClicked: home.recoveryRequested() }
        }
    }
}

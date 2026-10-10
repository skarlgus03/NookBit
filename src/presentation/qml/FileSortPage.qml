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
                color: pageDrop.containsDrag ? (home.dark ? "#304457" : "#eaf1f9") : home.surface
                border.color: pageDrop.containsDrag ? "#7898bb" : home.edge
                border.width: pageDrop.containsDrag ? UiMetrics.borderActive : UiMetrics.borderNormal
                Column {
                    anchors.centerIn: parent
                    spacing: UiMetrics.spaceSmall
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("＋ 이 페이지 어디든 파일을 놓으세요"); font.bold: true; font.pixelSize: UiMetrics.textEmphasis; color: home.foreground }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("대기 목록에만 추가돼요. 원본은 그대로 있어요"); font.pixelSize: UiMetrics.textBody; color: home.muted }
                }
            }
            RowLayout {
                Label { text: qsTr("대기 목록"); color: home.foreground; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: qsTr("%1개").arg(pendingFiles.count); color: home.muted }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(160, Math.min(240, home.height - 485))
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
                        width: files.width; height: UiMetrics.fileRowHeight
                        RowLayout {
                            anchors.fill: parent
                            spacing: UiMetrics.spaceGroup
                            Image {
                                objectName: "pendingFileIcon"
                                Layout.preferredWidth: UiMetrics.filePreviewSize
                                Layout.preferredHeight: UiMetrics.filePreviewSize
                                source: "image://filepreviews/" + encodeURIComponent(fullPath)
                                sourceSize: Qt.size(UiMetrics.filePreviewSize, UiMetrics.filePreviewSize)
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                opacity: available ? 1 : 0.45
                            }
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
                spacing: UiMetrics.spaceGroup
                FlatButton {
                    objectName: "compactPreviewButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: UiMetrics.largeActionHeight
                    text: qsTr("결과 미리보기")
                    contentItem: Text { text: parent.text; color: home.foreground; font.pixelSize: UiMetrics.textEmphasis; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0
                    onClicked: { previewController.createPreview(destination.text); home.pageRequested(2) }
                }
                FlatButton {
                    objectName: "compactSortButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: UiMetrics.largeActionHeight
                    text: qsTr("정리하기")
                    enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0 && !previewController.awaitingConfirmation
                    contentItem: Text { text: parent.text; color: parent.enabled ? "#ffffff" : home.muted; font.pixelSize: UiMetrics.textEmphasis; font.bold: true; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: UiMetrics.radiusCard; color: parent.enabled ? "#405e80" : home.surface; border.color: parent.hovered || parent.visualFocus ? "#88a8c9" : home.edge; border.width: parent.hovered || parent.visualFocus ? UiMetrics.borderActive : UiMetrics.borderNormal }
                    // 미리보기·준비 후 기존 최종 확인을 거쳐야만 실제 파일 이동을 시작하는 함수.
                    onClicked: {
                        previewController.createPreview(destination.text)
                        if (previewController.hasPreview && previewController.hasMovableSteps) previewController.prepareSort()
                    }
                }
            }
            FlatButton {
                objectName: "compactUndoButton"
                text: qsTr("되돌리기")
                enabled: previewController.canUndo
                onClicked: previewController.undoLastMove()
            }
            Label { Layout.fillWidth: true; text: previewController.message; visible: text.length > 0; wrapMode: Text.Wrap; color: home.muted; font.pixelSize: UiMetrics.textBody }
            Label { Layout.fillWidth: true; visible: pendingFiles.unavailableCount > 0; text: qsTr("찾을 수 없는 파일을 목록에서 빼주세요."); wrapMode: Text.Wrap; color: "#bc6257"; font.pixelSize: UiMetrics.textBody }
            FlatButton { text: qsTr("중단 작업 확인"); visible: recoveryController.recoveryCases.length > 0; onClicked: home.recoveryRequested() }
        }
    }
    DropArea {
        id: pageDrop
        objectName: "fileSortPageDropArea"
        anchors.fill: parent
        z: 10
        enabled: home.visible && home.enabled
        // 외부 파일 드래그만 받아 목록 등록에 복사 응답을 사용하는 함수.
        onEntered: function(drag) {
            if (drag.hasUrls && (drag.supportedActions & Qt.CopyAction)) drag.accept(Qt.CopyAction)
            else drag.accepted = false
        }
        // 페이지 어느 위치에 놓아도 파일을 이동하지 않고 대기 목록에 등록하는 함수.
        onDropped: function(event) {
            if (event.hasUrls && (event.supportedActions & Qt.CopyAction)) {
                for (const url of event.urls) pendingFiles.addUrl(url)
                event.accept(Qt.CopyAction)
            } else event.accepted = false
        }
        Rectangle {
            objectName: "fileDropOverlay"
            anchors.fill: parent
            visible: pageDrop.containsDrag
            color: Qt.rgba(1, 1, 1, UiMetrics.dropOverlayOpacity)
            radius: UiMetrics.radiusControl
            Rectangle {
                anchors.centerIn: parent
                width: Math.min(parent.width - UiMetrics.editorInset * 2, UiMetrics.dropHintMaxWidth)
                height: dropHint.implicitHeight + UiMetrics.editorInset * 2
                radius: UiMetrics.radiusCard
                color: home.surface
                border.color: home.edge
                Text {
                    id: dropHint
                    anchors.centerIn: parent
                    width: parent.width - UiMetrics.editorInset * 2
                    text: qsTr("파일을 놓으면 대기목록에 들어갑니다")
                    color: home.foreground
                    font.pixelSize: UiMetrics.textDropHint
                    font.bold: true
                    wrapMode: Text.Wrap
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}

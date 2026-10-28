import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 입력·목록 표시를 소유하고 미리보기·정리는 상위 작업 흐름에 요청하는 화면.
Item {
    id: input
    objectName: "fileSortScroll"
    required property var controller
    required property var pending
    required property var recovery
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property bool hasUnavailable: pending.unavailableCount > 0
    readonly property string notice: [pending.storageNotice,
        hasUnavailable ? qsTr("찾을 수 없는 파일을 목록에서 빼주세요.") : "", controller.message]
        .filter(part => part.length > 0).join(" · ")
    signal previewRequested(string destination)
    signal sortRequested(string destination)
    signal undoRequested()
    signal rulesRequested()
    signal recoveryRequested()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        spacing: UiMetrics.spaceGroup
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: UiMetrics.spaceSection
            rowSpacing: UiMetrics.spaceControl
            Label {
                text: qsTr("분류 규칙")
                font.pixelSize: UiMetrics.textBody
                color: input.theme.muted
            }
            Label {
                text: qsTr("정리할 위치")
                font.pixelSize: UiMetrics.textBody
                color: input.theme.muted
            }
            AppButton {
                id: rules
                objectName: "fileSortRuleButton"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                appearance: AppButton.Surface
                dark: input.dark
                text: input.controller.activeRuleLabel
                Accessible.name: qsTr("분류 규칙 선택: %1").arg(text)
                onClicked: input.rulesRequested()
                contentItem: RowLayout {
                    spacing: UiMetrics.spaceControl
                    NavigationIcon {
                        Layout.preferredWidth: UiMetrics.iconSize
                        Layout.preferredHeight: UiMetrics.iconSize
                        kind: "rules"
                        strokeColor: rules.buttonForeground
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: rules.text
                        textFormat: Text.PlainText
                        font: rules.font
                        color: rules.buttonForeground
                        elide: Text.ElideMiddle
                    }
                    NavigationIcon {
                        Layout.preferredWidth: UiMetrics.iconSize
                        Layout.preferredHeight: UiMetrics.iconSize
                        kind: "forward"
                        strokeColor: rules.buttonForeground
                    }
                }
            }
            AppTextField {
                id: destination
                objectName: "compactDestinationField"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.minimumWidth: 0
                text: input.controller.defaultDestinationRoot
                placeholderText: qsTr("정리 폴더의 전체 경로")
                color: input.theme.text
                Accessible.name: qsTr("정리할 위치")
                onTextEdited: input.controller.clear()
                HoverHandler { id: destinationHover }
                ToolTip.visible: destinationHover.hovered && text.length > 0
                ToolTip.delay: UiMetrics.tooltipDelayMs
                ToolTip.text: text
            }
        }
        Rectangle {
            id: queue
            objectName: "pendingQueueCard"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: UiMetrics.fileSortListHeaderHeight + UiMetrics.pendingRowHeight * 3
            color: input.theme.window
            radius: UiMetrics.radiusCard
            RowLayout {
                id: queueHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: UiMetrics.spaceSection
                anchors.rightMargin: UiMetrics.spaceSection
                height: UiMetrics.fileSortListHeaderHeight
                spacing: UiMetrics.spaceControl
                Label {
                    text: qsTr("대기 목록")
                    font.pixelSize: UiMetrics.textEmphasis
                    font.bold: true
                    color: input.theme.text
                }
                Label {
                    text: qsTr("%1개").arg(input.pending.count)
                    color: input.theme.muted
                    font.pixelSize: UiMetrics.textBody
                }
                Item { Layout.fillWidth: true }
                Label {
                    visible: input.pending.count > 0
                    text: qsTr("파일이나 폴더를 놓으세요")
                    color: input.theme.muted
                    font.pixelSize: UiMetrics.textBody
                }
            }
            ColumnLayout {
                objectName: "pendingEmptyState"
                visible: input.pending.count === 0
                anchors.centerIn: files
                width: Math.min(files.width - UiMetrics.spaceSection * 2, UiMetrics.dropHintMaxWidth)
                spacing: UiMetrics.spaceGroup
                NavigationIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: UiMetrics.filePreviewSize
                    Layout.preferredHeight: UiMetrics.filePreviewSize
                    kind: "folder"
                    strokeColor: input.theme.accent
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("파일이나 폴더를 끌어놓으세요")
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: UiMetrics.textEmphasis
                    font.bold: true
                    wrapMode: Text.Wrap
                    color: input.theme.text
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("이 창 어디든 놓을 수 있어요.\n대기 목록에만 추가되고 원본은 그대로예요.")
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: UiMetrics.textBody
                    wrapMode: Text.Wrap
                    color: input.theme.muted
                }
            }
            ListView {
                id: files
                objectName: "pendingQueueList"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: queueHeader.bottom
                anchors.bottom: parent.bottom
                anchors.margins: UiMetrics.spaceControl
                model: input.pending
                clip: true
                spacing: UiMetrics.spaceExtraSmall
                ScrollBar.vertical: ScrollBar {}
                delegate: RowLayout {
                    required property int index
                    required property string fileName
                    required property string fullPath
                    required property bool available
                    required property bool directory
                    readonly property string extension: fileName.lastIndexOf(".") > 0 ? fileName.slice(fileName.lastIndexOf(".") + 1).toUpperCase() : ""
                    width: files.width - UiMetrics.spaceGroup
                    height: UiMetrics.pendingRowHeight
                    spacing: UiMetrics.spaceGroup
                    Image {
                        objectName: "pendingFileIcon"
                        Layout.preferredWidth: UiMetrics.pendingPreviewSize
                        Layout.preferredHeight: UiMetrics.pendingPreviewSize
                        source: "image://filepreviews/" + encodeURIComponent(fullPath)
                        sourceSize: Qt.size(UiMetrics.filePreviewSize, UiMetrics.filePreviewSize)
                        asynchronous: true
                        fillMode: Image.PreserveAspectFit
                        opacity: available ? 1 : 0.45
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        spacing: UiMetrics.spaceTiny
                        Text {
                            Layout.fillWidth: true
                            text: available ? fileName : qsTr("[없음] %1").arg(fileName)
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            font.pixelSize: UiMetrics.textEmphasis
                            font.weight: Font.DemiBold
                            color: available ? input.theme.text : input.theme.danger
                        }
                        Text {
                            Layout.fillWidth: true
                            text: fullPath
                            textFormat: Text.PlainText
                            elide: Text.ElideMiddle
                            font.pixelSize: UiMetrics.textBody
                            color: input.theme.muted
                        }
                        HoverHandler { id: pathHover }
                        ToolTip.visible: pathHover.hovered
                        ToolTip.delay: UiMetrics.tooltipDelayMs
                        ToolTip.text: fullPath
                    }
                    Label {
                        Layout.preferredWidth: UiMetrics.pendingTypeWidth
                        text: directory ? qsTr("폴더") : (extension.length > 0 ? extension : qsTr("파일"))
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                        font.pixelSize: UiMetrics.textBody
                        color: input.theme.muted
                    }
                    AppButton {
                        text: "×"
                        flat: true
                        dark: input.dark
                        implicitWidth: UiMetrics.iconButtonSize
                        Accessible.name: qsTr("%1 목록에서 빼기").arg(fileName)
                        onClicked: input.pending.removeAt(index)
                    }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: input.notice.length > 0 || input.recovery.recoveryCases.length > 0
            spacing: UiMetrics.spaceControl
            Label {
                objectName: "fileSortNotice"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: input.notice
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font.pixelSize: UiMetrics.textBody
                color: input.pending.storageNotice.length > 0 || input.hasUnavailable ? input.theme.danger : input.theme.muted
                HoverHandler { id: noticeHover }
                ToolTip.visible: noticeHover.hovered && input.notice.length > 0
                ToolTip.delay: UiMetrics.tooltipDelayMs
                ToolTip.text: input.notice
            }
            AppButton {
                text: qsTr("중단 작업 확인")
                visible: input.recovery.recoveryCases.length > 0
                flat: true
                dark: input.dark
                onClicked: input.recoveryRequested()
            }
        }
        Item {
            objectName: "fileSortActionBar"
            Layout.fillWidth: true
            Layout.preferredHeight: UiMetrics.fileSortFooterHeight
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: UiMetrics.borderNormal
                color: input.theme.border
            }
            RowLayout {
                id: actions
                readonly property real actionWidth: Math.max(UiMetrics.fileSortActionMinWidth, previewButton.implicitWidth, sortButton.implicitWidth)
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: UiMetrics.spaceGroup
                AppButton {
                    id: undo
                    objectName: "compactUndoButton"
                    text: qsTr("되돌리기")
                    flat: true
                    dark: input.dark
                    enabled: input.controller.canUndo
                    onClicked: input.undoRequested()
                    contentItem: RowLayout {
                        spacing: UiMetrics.spaceControl
                        NavigationIcon {
                            Layout.preferredWidth: UiMetrics.iconSize
                            Layout.preferredHeight: UiMetrics.iconSize
                            kind: "undo"
                            strokeColor: undo.buttonForeground
                        }
                        Text { text: undo.text; font: undo.font; color: undo.buttonForeground }
                    }
                }
                Item { Layout.fillWidth: true }
                LargeActionButton {
                    id: previewButton
                    objectName: "compactPreviewButton"
                    Layout.preferredWidth: actions.actionWidth
                    dark: input.dark
                    text: qsTr("결과 미리보기")
                    enabled: input.pending.count > 0 && !input.hasUnavailable
                    onClicked: input.previewRequested(destination.text)
                }
                LargeActionButton {
                    id: sortButton
                    objectName: "compactSortButton"
                    Layout.preferredWidth: actions.actionWidth
                    dark: input.dark
                    primary: true
                    text: qsTr("정리하기")
                    enabled: input.pending.count > 0 && !input.hasUnavailable && !input.controller.awaitingConfirmation
                    onClicked: input.sortRequested(destination.text)
                }
            }
        }
    }
}

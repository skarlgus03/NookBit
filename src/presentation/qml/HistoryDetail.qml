import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 선택 기록의 요약·파일 경로를 표시하고 되돌리기 확인만 요청하는 상세 화면.
Item {
    id: detail
    property var entry: null
    property var steps: []
    property string message: ""
    property bool canUndo: false
    property int undoCount: 0
    property bool busy: false
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property int appliedCount: steps.filter(step => step.applied === true).length
    readonly property int rolledBackCount: steps.filter(step => step.rolledBack === true).length
    signal undoRequested()

    // 경로의 파일 이름을 제외한 폴더 부분을 반환하는 함수.
    function folder(path) {
        const normalized = String(path || "").replace(/\\/g, "/")
        const slash = normalized.lastIndexOf("/")
        return slash >= 0 ? normalized.slice(0, slash + 1) : normalized
    }
    // 원본 폴더는 마지막 이름으로 줄이고 전체 경로는 툴팁에서 제공하는 함수.
    function folderName(path) {
        const parent = folder(path).replace(/\/+$/, "")
        return parent.slice(parent.lastIndexOf("/") + 1) || folder(path)
    }
    component SummaryCard: Rectangle {
        property string label: ""
        property int count: 0
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: UiMetrics.historySummaryHeight
        radius: UiMetrics.radiusCard
        color: detail.theme.surface
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: UiMetrics.spaceSection
            spacing: UiMetrics.spaceExtraSmall
            Label { text: label; color: detail.theme.muted; font.pixelSize: UiMetrics.textBody }
            Label { text: qsTr("%1개").arg(count); color: detail.theme.text; font.pixelSize: UiMetrics.textSectionTitle; font.bold: true }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        spacing: UiMetrics.spaceSection
        RowLayout {
            Layout.fillWidth: true
            visible: detail.entry !== null
            spacing: UiMetrics.spaceGroup
            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: detail.entry ? detail.entry.detailTime || detail.entry.time : ""
                font.pixelSize: UiMetrics.textSectionTitle
                font.bold: true
                color: detail.theme.text
                elide: Text.ElideRight
            }
            Rectangle {
                readonly property string tone: detail.entry ? detail.entry.statusTone || "neutral" : "neutral"
                implicitWidth: statusLabel.implicitWidth + UiMetrics.spaceGroup * 2
                implicitHeight: statusLabel.implicitHeight + UiMetrics.spaceExtraSmall * 2
                radius: height / 2
                color: detail.theme[tone + "Surface"] || detail.theme.alternate
                Label {
                    id: statusLabel
                    anchors.centerIn: parent
                    text: detail.entry ? detail.entry.status : ""
                    color: detail.theme[parent.tone] || detail.theme.muted
                    font.pixelSize: UiMetrics.textBody
                    font.bold: true
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: detail.entry !== null
            spacing: UiMetrics.spaceControl
            SummaryCard { label: qsTr("대상"); count: detail.entry ? detail.entry.fileCount : 0 }
            SummaryCard { objectName: "historyAppliedSummary"; label: qsTr("현재 이동됨"); count: detail.appliedCount }
            SummaryCard { objectName: "historyRolledBackSummary"; label: qsTr("되돌림"); count: detail.rolledBackCount }
        }
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width
                visible: detail.entry === null
                spacing: UiMetrics.spaceGroup
                NavigationIcon {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: UiMetrics.filePreviewSize
                    Layout.preferredHeight: UiMetrics.filePreviewSize
                    kind: "history"
                    strokeColor: detail.theme.muted
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("왼쪽에서 작업을 선택해 주세요.")
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: detail.theme.muted
                    font.pixelSize: UiMetrics.textEmphasis
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Math.min(parent.height, files.contentHeight + UiMetrics.spaceSection * 2)
                visible: detail.entry !== null
                color: detail.theme.surface
                radius: UiMetrics.radiusCard
                ListView {
                    id: files
                    objectName: "historyFileList"
                    anchors.fill: parent
                    anchors.margins: UiMetrics.spaceSection
                    model: detail.steps
                    clip: true
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        required property var modelData
                        width: files.width - UiMetrics.spaceControl
                        height: Math.max(UiMetrics.fileRowHeight, fileText.implicitHeight + UiMetrics.spaceGroup)
                        spacing: UiMetrics.spaceGroup
                        FileTypeBadge {
                            objectName: "historyFileType_" + modelData.fileName
                            Layout.preferredWidth: UiMetrics.historyTypeTileSize
                            Layout.preferredHeight: UiMetrics.historyTypeTileSize
                            fileName: modelData.fileName
                            dark: detail.dark
                        }
                        ColumnLayout {
                            id: fileText
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: UiMetrics.spaceExtraSmall
                            Label {
                                Layout.fillWidth: true
                                text: modelData.fileName
                                textFormat: Text.PlainText
                                font.pixelSize: UiMetrics.textEmphasis
                                font.weight: Font.DemiBold
                                color: modelData.searchMatch === true ? detail.theme.accent : detail.theme.text
                                elide: Text.ElideMiddle
                            }
                            Label {
                                objectName: "historySearchMatch_" + modelData.fileName
                                visible: modelData.searchMatch === true
                                text: qsTr("검색 일치")
                                color: detail.theme.accent
                                font.pixelSize: UiMetrics.textBody
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: UiMetrics.spaceSmall
                                Label {
                                    Layout.maximumWidth: files.width / 3
                                    text: detail.folderName(modelData.source)
                                    textFormat: Text.PlainText
                                    elide: Text.ElideMiddle
                                    color: detail.theme.muted
                                    font.pixelSize: UiMetrics.textBody
                                }
                                NavigationIcon {
                                    Layout.preferredWidth: UiMetrics.iconSize
                                    Layout.preferredHeight: UiMetrics.iconSize
                                    kind: "forward"
                                    strokeColor: detail.theme.accent
                                }
                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 0
                                    text: detail.folder(modelData.destination)
                                    textFormat: Text.PlainText
                                    elide: Text.ElideMiddle
                                    color: detail.theme.accent
                                    font.pixelSize: UiMetrics.textBody
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.status
                                textFormat: Text.PlainText
                                wrapMode: Text.WrapAnywhere
                                color: detail.theme[modelData.statusTone] || detail.theme.muted
                                font.pixelSize: UiMetrics.textBody
                            }
                            HoverHandler { id: fileHover }
                            ToolTip.visible: fileHover.hovered
                            ToolTip.delay: UiMetrics.tooltipDelayMs
                            ToolTip.text: qsTr("%1\n원본: %2\n목적지: %3").arg(modelData.fileName).arg(modelData.source).arg(modelData.destination)
                        }
                    }
                }
            }
        }
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: UiMetrics.historyFooterHeight
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: UiMetrics.borderNormal
                color: detail.theme.border
            }
            RowLayout {
                anchors.fill: parent
                anchors.topMargin: UiMetrics.spaceSection
                spacing: UiMetrics.spaceGroup
                Label {
                    id: notice
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: detail.canUndo ? qsTr("되돌리면 파일 %1개를 원래 위치로 옮겨요.").arg(detail.undoCount) : detail.message
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: detail.theme.muted
                    font.pixelSize: UiMetrics.textBody
                    HoverHandler { id: noticeHover }
                    ToolTip.visible: noticeHover.hovered
                    ToolTip.text: detail.message
                    ToolTip.delay: UiMetrics.tooltipDelayMs
                }
                AppButton {
                    id: undoAction
                    objectName: "historyUndoButton"
                    text: detail.canUndo ? qsTr("이 작업 되돌리기 (%1개)").arg(detail.undoCount) : qsTr("이 작업 되돌리기")
                    appearance: AppButton.Soft
                    dark: detail.dark
                    font.bold: true
                    enabled: detail.entry !== null && detail.canUndo && !detail.busy
                    onClicked: detail.undoRequested()
                    contentItem: RowLayout {
                        spacing: UiMetrics.spaceControl
                        NavigationIcon {
                            Layout.preferredWidth: UiMetrics.iconSize
                            Layout.preferredHeight: UiMetrics.iconSize
                            kind: "undo"
                            strokeColor: undoAction.buttonForeground
                        }
                        Text { text: undoAction.text; font: undoAction.font; color: undoAction.buttonForeground }
                    }
                }
            }
        }
    }
}

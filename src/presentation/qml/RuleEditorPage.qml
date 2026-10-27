import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 단일 편집 상태와 화면 부품의 연결만 담당하는 규칙 편집 페이지.
Item {
    id: page
    required property var controller
    required property var destinations
    required property var guideService
    property bool oldEditorFormDirty: false
    property bool darkMode: false
    property bool pageInitialized: false
    signal openCurrentEditor
    signal editorDraftReplaced
    property alias blocks: session.blocks
    property alias edges: session.edges
    property alias zoom: session.zoom
    readonly property alias selectedBlock: session.selectedBlock
    readonly property alias nodeAddGroups: session.nodeAddGroups
    readonly property var theme: UiTheme.colors(darkMode)
    palette.window: theme.window
    palette.windowText: theme.text
    palette.button: theme.button
    palette.buttonText: theme.text
    palette.base: theme.nodeSurface
    palette.text: theme.text
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText

    // 저장된 초안을 같은 편집 세션에 다시 읽어오는 함수.
    function loadGraphFromDraft() {
        documents.loadGraphFromDraft();
    }
    // 지정한 종류의 블록을 같은 편집 세션에 추가하는 함수.
    function addBlock(type, worldX, worldY) {
        session.addBlock(type, worldX, worldY);
    }
    // 지정한 캔버스 위치에 블록 추가 메뉴를 여는 함수.
    function openAddMenu(screenX, screenY) {
        canvas.openAddMenu(screenX, screenY);
    }
    // 페이지를 떠나면 각 소유자에게 보조 창 닫기를 요청하는 함수.
    function dismissTransientUi() {
        toolbar.dismissTransientUi();
        canvas.dismissTransientUi();
        inspector.dismissTransientUi();
        testPanel.dismissTransientUi();
        documents.dismissTransientUi();
    }
    onVisibleChanged: if (!visible && pageInitialized)
                          dismissTransientUi()
    Component.onCompleted: {
        loadGraphFromDraft();
        pageInitialized = true;
    }

    RuleEditorState {
        id: session
        viewportWidth: canvas.viewport.width
        viewportHeight: canvas.viewport.height
    }
    RuleDocumentActions {
        id: documents
        anchors.fill: parent
        editor: session
        controller: page.controller
        guideService: page.guideService
        oldEditorFormDirty: page.oldEditorFormDirty
        dark: page.darkMode
        onEditorDraftReplaced: page.editorDraftReplaced()
    }
    Rectangle {
        anchors.fill: parent
        color: page.theme.window
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        spacing: UiMetrics.spaceGroup
        RuleEditorToolbar {
            id: toolbar
            Layout.fillWidth: true
            editor: session
            canvasHost: canvas.viewport
            dark: page.darkMode
            testOpened: testPanel.visible
            guideStatus: documents.guideStatus
            onSaveRequested: documents.requestSave()
            onTestRequested: testPanel.visible ? testPanel.close() : testPanel.open()
            onImportRequested: documents.requestImport()
            onExportRequested: documents.requestExport()
            onReloadRequested: documents.requestReload()
            onGuideRequested: documents.saveGuide()
            onAddRequested: canvas.openAddMenu(canvas.viewport.width / 2, canvas.viewport.height / 2)
            onFocusCanvasRequested: canvas.focusCanvas()
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: UiMetrics.spaceCompact
            RuleCanvas {
                id: canvas
                Layout.fillWidth: true
                Layout.fillHeight: true
                editor: session
                dark: page.darkMode
            }
            RuleInspector {
                id: inspector
                Layout.preferredWidth: 238
                Layout.fillHeight: true
                editor: session
                destinations: page.destinations
                overlayHost: page
                dark: page.darkMode
            }
        }
        Label {
            id: editorStatus
            Layout.fillWidth: true
            text: session.statusText
            color: page.theme.muted
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            HoverHandler {
                id: statusHover
            }
            ToolTip.visible: statusHover.hovered && editorStatus.truncated
            ToolTip.text: editorStatus.text
            ToolTip.delay: UiMetrics.tooltipDelayMs
        }
    }
    RuleTestPanel {
        id: testPanel
        parent: canvas.viewport
        editor: session
        controller: page.controller
        dark: page.darkMode
    }
}

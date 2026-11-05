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
        inspection.close();
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
        anchors.margins: UiMetrics.spaceSection
        spacing: UiMetrics.spaceGroup
        RuleEditorToolbar {
            id: toolbar
            Layout.fillWidth: true
            editor: session
            canvasHost: canvas.viewport
            dark: page.darkMode
            testOpened: testPanel.visible
            guideStatus: documents.guideStatus
            hasUnsavedChanges: session.graphDirty || page.controller.dirty || page.oldEditorFormDirty
            onSaveRequested: documents.requestSave()
            onTestRequested: testPanel.visible ? testPanel.close() : testPanel.open()
            onInspectRequested: {
                inspection.report = page.controller.inspectGraph(session.blocks, session.edges);
                inspection.open();
            }
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
                Layout.preferredWidth: UiMetrics.ruleInspectorWidth
                Layout.fillHeight: true
                editor: session
                destinations: page.destinations
                overlayHost: page
                dark: page.darkMode
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: UiMetrics.ruleStatusHeight
            spacing: UiMetrics.spaceGroup
            Label {
                id: editorStatus
                objectName: "ruleEditorStatus"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: session.statusText
                textFormat: Text.PlainText
                font.pixelSize: UiMetrics.textBody
                color: page.theme.muted
                elide: Text.ElideRight
                HoverHandler { id: statusHover }
                ToolTip.visible: statusHover.hovered && editorStatus.truncated
                ToolTip.text: editorStatus.text
                ToolTip.delay: UiMetrics.tooltipDelayMs
            }
            Label {
                visible: session.selectedIds.length > 0
                text: qsTr("%1개 선택").arg(session.selectedIds.length)
                font.pixelSize: UiMetrics.textBody
                color: page.theme.accent
            }
        }
    }
    RuleTestPanel {
        id: testPanel
        parent: canvas.viewport
        editor: session
        controller: page.controller
        dark: page.darkMode
    }
    Dialog {
        id: inspection
        objectName: "ruleInspectionDialog"
        property var report: ({})
        readonly property var warnings: report.warnings || []
        parent: page
        anchors.centerIn: parent
        width: Math.max(1, Math.min(page.width - UiMetrics.spaceSection * 2, UiMetrics.ruleHelpWidth))
        height: Math.max(1, Math.min(implicitHeight, page.height - UiMetrics.spaceSection * 2))
        title: qsTr("규칙 검사")
        modal: true
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceControl
            Label {
                Layout.fillWidth: true
                text: inspection.report.error ? qsTr("구조 오류 · 저장할 수 없어요")
                    : inspection.warnings.length ? qsTr("경고 %1개 · 저장할 수 있어요").arg(inspection.warnings.length)
                    : qsTr("검사한 항목에서 오류나 경고를 찾지 못했어요.")
                font.bold: true
                wrapMode: Text.Wrap
                color: inspection.report.error ? page.theme.danger
                    : inspection.warnings.length ? page.theme.warning : page.theme.text
            }
            ScrollView {
                visible: !!inspection.report.error || inspection.warnings.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredHeight: inspectionRows.implicitHeight
                contentWidth: availableWidth
                clip: true
                Column {
                    id: inspectionRows
                    width: parent.width
                    spacing: UiMetrics.spaceExtraSmall
                    Repeater {
                        model: inspection.report.error
                            ? [{blockId: inspection.report.blockId, reason: inspection.report.error}]
                            : inspection.warnings
                        delegate: RuleEditorButton {
                            required property var modelData
                            objectName: "ruleInspectionRow"
                            width: inspectionRows.width
                            dark: page.darkMode
                            labelAlignment: Text.AlignLeft
                            enabled: modelData.blockId > 0
                            text: inspection.report.error ? modelData.reason
                                : qsTr("노드 %1 · %2").arg(modelData.blockId).arg(modelData.reason)
                            onClicked: {
                                inspection.close();
                                session.focusBlock(modelData.blockId);
                                canvas.focusCanvas();
                            }
                        }
                    }
                }
            }
        }
        Connections {
            target: session
            function onBlocksChanged() { inspection.close(); }
            function onEdgesChanged() { inspection.close(); }
        }
    }
}

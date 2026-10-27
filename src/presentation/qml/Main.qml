import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    font.pixelSize: UiMetrics.textBody
    // 트레이 종료에서도 마지막 캐릭터 위치와 열린 페이지 크기를 저장하는 함수.
    function commitCharacterPosition() { desktopPet.commitPosition(); extensionWindow.commitSize() }
    // 닫기는 트레이에 숨기고 프로세스 종료 여부는 별도 정책에 맡기는 함수.
    onClosing: function(close) {
        close.accepted = false
        desktopPet.commitPosition()
        extensionWindow.commitSize()
        appLifecycle.closeWindow()
    }
    CharacterWindow {
        id: desktopPet
        placement: characterWindow
        descriptor: characterController.animation
        bubbleSettings: characterController.bubble
        dark: root.editorDark
        message: characterController.message
        interactions: companionInteractions
        onGestureHeldChanged: companionMotion.setPointerHeld(gestureHeld)
        onVisualBoundsUpdated: function(left, right) { companionMotion.setVisualBounds(left, right) }
        userScale: characterSettings.userScale
        userPivotOffset: characterSettings.pivotOffset
        userBubbleOffset: characterSettings.bubbleOffset
        actionAvailable: characterController.reactionId === "" && notifications.activeTypes.includes("file.offer") && !backgroundTasks.busy
        onActionRequested: {
            if (characterController.reactionId !== "" || !notifications.activeTypes.includes("file.offer") || !watchController.proposalVisible || backgroundTasks.busy) return
            appLifecycle.openWindow()
            watchProposalDialog.open()
        }
        onSettingsRequested: {
            root.showNormal()
            root.raise()
            root.requestActivate()
            root.openCharacterSettings()
        }
    }
    OperationProgressPopup {
        id: operationProgressPopup
        parent: extensionWindow.contentItem
        progress: backgroundTasks
        dark: root.editorDark
        visible: backgroundTasks.busy && extensionWindow.visible && !root.inlineRecoveryInspection
                 && !(root.currentPage === 7 && fileSortPane.workflowStep === 3)
        x: (parent.width - width) / 2
        y: extensionHost.y + (extensionHost.height - height) / 2
    }
    property int currentPage: 0
    // 복구 목록의 읽기 전용 검사는 전체 오버레이 대신 해당 페이지 안에서 표시한다.
    readonly property bool inlineRecoveryInspection: currentPage === 10 && backgroundTasks.busy && !backgroundTasks.fileWorkActive
    readonly property bool inlineSortConfirmation: currentPage === 7 && fileSortPane.manualSortRequested
    // 확인을 인라인 화면으로 넘길 때 기존 대화상자를 닫되 승인 대기는 취소하지 않는 함수.
    onInlineSortConfirmationChanged: if (inlineSortConfirmation && sortConfirmation.visible) sortConfirmation.close()
    readonly property int homeWidth: 380
    // 본체 크기는 유지하고 별도 연결 창의 페이지만 전환하는 함수.
    onCurrentPageChanged: {
        // 예전 독립 미리보기 번호는 파일 정리로 돌려 빈 페이지를 만들지 않는다.
        if (currentPage === 2) { currentPage = 7; return }
    }
    readonly property bool editorDark: uiSettings.darkMode
    readonly property var theme: UiTheme.colors(editorDark)
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted
    readonly property color surfaceColor: theme.surface
    readonly property color borderColor: theme.border
    palette.window: theme.window
    palette.windowText: primaryText
    palette.text: primaryText
    palette.buttonText: primaryText
    palette.button: theme.button
    palette.base: surfaceColor
    palette.alternateBase: theme.alternate
    palette.mid: borderColor
    palette.highlight: theme.accent
    palette.highlightedText: theme.accentText
    palette.disabled.text: mutedText
    palette.disabled.buttonText: mutedText

    // 감시 입력 초안의 복원은 페이지에 맡기고 해당 화면을 여는 함수.
    function openWatchSettings() { currentPage = 3 }
    // 복구 화면을 먼저 연 뒤 파일을 바꾸지 않는 목록 검사를 요청하는 함수.
    function openRecovery() { currentPage = 10; recoveryController.refreshRecoveryCases() }

    // 최신 작업 기록을 조회하고 별도 창을 여는 함수.
    function openHistory() {
        currentPage = 4
        historyController.refreshHistory()
        historyDialog.open()
    }
    // 캐릭터 설정을 오른쪽 확장 영역에 여는 함수.
    function openCharacterSettings() {
        currentPage = 5
        characterSettingsDialog.open()
    }

    width: homeWidth
    height: Math.min(UiMetrics.homeDefaultHeight, Screen.desktopAvailableHeight - 60)
    minimumWidth: homeWidth
    maximumWidth: homeWidth
    minimumHeight: 420
    visible: true
    title: qsTr("NookBit")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    CompactHome {
        id: compactHome
        pendingCount: pendingFiles.count
        watchEnabled: watchController.enabled
        watchFolder: watchController.folder
        companionDescriptor: characterController.animation
        navigationEnabled: !backgroundTasks.busy && !previewController.awaitingConfirmation
        currentPage: root.currentPage
        width: root.homeWidth
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        dark: root.editorDark
        // 선택한 페이지를 본체와 별개인 연결 창에 표시하는 함수.
        onPageRequested: function(page) { root.currentPage = page }
        onMoveRequested: root.startSystemMove()
        onMinimizeRequested: root.showMinimized()
        onCloseRequested: root.close()
        onHistoryRequested: root.openHistory()
    }
    MouseArea {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 7
        cursorShape: Qt.SizeVerCursor
        // 제목 표시줄 없이도 창 아래쪽에서 높이를 조절하는 함수.
        onPressed: root.startSystemResize(Qt.BottomEdge)
    }

    ApplicationWindow {
        id: extensionWindow
        objectName: "extensionWindow"
        transientParent: root
        flags: Qt.Tool | Qt.FramelessWindowHint
        color: "transparent"
        palette: root.palette
        font: root.font
        title: qsTr("NookBit 확장 페이지")
        property int activePage: 0
        property bool restoringSize: false
        width: UiMetrics.extensionDefaults[1].width
        height: UiMetrics.extensionDefaults[1].height
        // 크기 조절 중에는 저장을 늦추고 페이지 전환·닫기 때는 즉시 확정하는 함수.
        function commitSize() {
            sizeSaveTimer.stop()
            if (activePage > 0 && !restoringSize)
                uiSettings.rememberExtensionSize(activePage, width, height)
        }
        // 이전 페이지 크기를 보존한 뒤 새 페이지의 기본값 또는 마지막 크기를 복원하는 함수.
        function activatePage(page) {
            commitSize()
            activePage = page
            if (page === 0) return
            const spec = UiMetrics.extensionDefaults[page]
            if (!spec) return
            restoringSize = true
            minimumWidth = spec.minWidth
            minimumHeight = spec.minHeight
            const saved = uiSettings.extensionSize(page, Qt.size(spec.width, spec.height))
            width = Math.max(minimumWidth, Math.min(saved.width, Screen.desktopAvailableWidth - UiMetrics.spaceSection * 2))
            height = Math.max(minimumHeight, Math.min(saved.height, Screen.desktopAvailableHeight - UiMetrics.spaceSection * 2))
            restoringSize = false
        }
        onWidthChanged: if (activePage > 0 && !restoringSize) sizeSaveTimer.restart()
        onHeightChanged: if (activePage > 0 && !restoringSize) sizeSaveTimer.restart()
        Timer {
            id: sizeSaveTimer
            interval: 300
            // 크기 조절이 잠시 멈추면 마지막 크기를 저장하는 함수.
            onTriggered: extensionWindow.commitSize()
        }
        Connections {
            target: root
            // 페이지를 전환할 때 해당 페이지의 저장 크기를 복원하는 함수.
            function onCurrentPageChanged() { extensionWindow.activatePage(root.currentPage) }
        }
        // 개별 모니터 경계로 제한하지 않고 가상 데스크톱에서 본체 오른쪽을 따라간다.
        readonly property real dockX: root.x + root.width + 10
        x: dockX
        y: root.y
        visible: root.currentPage !== 0 && root.visible && root.visibility !== Window.Minimized
        // 확장 창의 닫기는 앱 종료가 아니라 페이지 숨기기로 처리하는 함수.
        onClosing: function(event) { event.accepted = false; root.currentPage = 0 }
    }
    Repeater {
        parent: extensionWindow.contentItem
        model: [
            {edges: Qt.RightEdge, horizontal: 1, vertical: 0, cursor: Qt.SizeHorCursor},
            {edges: Qt.BottomEdge, horizontal: 0, vertical: 1, cursor: Qt.SizeVerCursor},
            {edges: Qt.RightEdge | Qt.BottomEdge, horizontal: 1, vertical: 1, cursor: Qt.SizeFDiagCursor}
        ]
        delegate: MouseArea {
            required property var modelData
            objectName: "extensionResizeEdge"
            readonly property bool corner: modelData.horizontal !== 0 && modelData.vertical !== 0
            width: modelData.horizontal !== 0 ? UiMetrics.windowResizeGrip * (corner ? 2 : 1) : parent.width
            height: modelData.vertical !== 0 ? UiMetrics.windowResizeGrip * (corner ? 2 : 1) : parent.height
            x: modelData.horizontal > 0 ? parent.width - width : 0
            y: modelData.vertical > 0 ? parent.height - height : 0
            z: 200
            cursorShape: modelData.cursor
            // 본체에 붙는 위치는 유지하고 오른쪽·아래쪽에서 시스템 크기 조절을 시작하는 함수.
            onPressed: extensionWindow.startSystemResize(modelData.edges)
        }
    }
    Rectangle {
        id: extensionPanel
        objectName: "extensionPanel"
        parent: extensionWindow.contentItem
        anchors.fill: parent
        visible: root.currentPage !== 0
        color: root.theme.surface
        border.color: root.theme.border
        radius: UiMetrics.radiusControl
        z: 0
    }
    Item {
        parent: extensionWindow.contentItem
        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
        height: 58
        z: 100
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.margins: UiMetrics.spaceGroup; height: 36
            Label {
                Layout.fillWidth: true
                text: ["", qsTr("분류 규칙"), qsTr("분류 결과"), qsTr("폴더 감시 설정"), qsTr("작업 기록"), qsTr("컴패니언"), qsTr("이름 충돌 설정"), qsTr("파일 정리"), qsTr("설정"), qsTr("개발 도구"), qsTr("중단 작업 확인"), qsTr("알림")][root.currentPage]
                color: root.theme.text
                font.pixelSize: UiMetrics.textWindowTitle; font.bold: true
            }
            BackButton { description: qsTr("설정으로 돌아가기"); enabled: !backgroundTasks.busy; visible: [3, 6, 10, 11].indexOf(root.currentPage) >= 0; onClicked: root.currentPage = 8 }
            NavigationButton { objectName: "extensionCloseButton"; text: qsTr("접기 ×"); enabled: !backgroundTasks.busy; onClicked: root.currentPage = 0 }
        }
    }
    Item {
        id: extensionHost
        enabled: (!backgroundTasks.busy || (root.currentPage === 7 && fileSortPane.workflowStep === 3)) && (!previewController.awaitingConfirmation || root.inlineSortConfirmation)
        parent: extensionWindow.contentItem
        anchors.fill: parent
        anchors.topMargin: 58
        visible: root.currentPage !== 0
        clip: true
        z: 1
    }
    // 검사·되돌리기 진행 표시는 서브창 본문만 밝게 덮고 창 이동 영역은 남긴다.
    Rectangle {
        objectName: "operationProgressVeil"
        parent: extensionWindow.contentItem
        anchors.fill: extensionHost
        visible: operationProgressPopup.visible
        color: root.theme.inspectionVeil
        opacity: UiMetrics.operationVeilOpacity
        z: 90
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        context: Qt.ApplicationShortcut
        enabled: !backgroundTasks.busy && !previewController.awaitingConfirmation
        // 사용자 화면과 분리된 개발 도구 페이지를 여는 함수.
        onActivated: root.currentPage = root.currentPage === 9 ? 0 : 9
    }
    CollisionSettingsPage {
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        visible: root.currentPage === 6
        dark: root.editorDark
        controller: previewController
    }

    FileSortPage {
        id: fileSortPane
        controller: previewController
        pending: pendingFiles
        recovery: recoveryController
        progress: backgroundTasks
        parent: extensionHost
        anchors.fill: parent
        visible: root.currentPage === 7
        dark: root.editorDark
        // 파일 정리에서 요청한 규칙 편집 페이지를 연결 창에 표시하는 함수.
        onPageRequested: function(page) { root.currentPage = page }
        onRecoveryRequested: { root.openRecovery() }
    }
    NotificationSettingsPage {
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        visible: root.currentPage === 11
        dark: root.editorDark
        types: notifications.types
        status: notifications.status
        onEnabledRequested: function(typeId, value) { notifications.setEnabled(typeId, value) }
    }
    AppSettingsPage {
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        visible: root.currentPage === 8
        dark: root.editorDark
        settings: uiSettings
        destinations: destinationSettings
        recoveryAvailable: recoveryController.recoveryCases.length > 0
        onWatchSettingsRequested: root.openWatchSettings()
        onCollisionSettingsRequested: root.currentPage = 6
        onRecoveryRequested: root.openRecovery()
    }

    DeveloperToolsPage {
        parent: extensionHost
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 28
        anchors.topMargin: 10
        anchors.bottomMargin: 28
        visible: root.currentPage === 9
        dark: root.editorDark
        controller: previewController
        pending: pendingFiles
        recovery: recoveryController
        onBubbleTestRequested: { characterWindow.setShown(true); desktopPet.testBubble() }
        onLegacyEditorRequested: ruleDialog.open()
        onRulesRequested: root.currentPage = 1
        onRecoveryRequested: root.openRecovery()
    }

    RuleEditorPage {
        id: ruleEditorPage
        controller: ruleEditor
        destinations: destinationSettings
        guideService: userGuides
        parent: extensionHost
        objectName: "ruleEditorPage"
        oldEditorFormDirty: ruleDialog.dirty
        anchors.fill: parent
        anchors.leftMargin: 0
        anchors.topMargin: 0
        visible: root.currentPage === 1
        darkMode: root.editorDark
        onOpenCurrentEditor: ruleDialog.open()
        onEditorDraftReplaced: ruleDialog.resetDraftForm()
    }

    Connections {
        target: previewController
        // 목록·규칙·설정 변경으로 확인이 무효화되면 창도 닫는 함수.
        function onPreviewChanged() {
            if (previewController.awaitingConfirmation && !root.inlineSortConfirmation && !sortConfirmation.visible)
                sortConfirmation.open()
            else if (!previewController.awaitingConfirmation && sortConfirmation.visible)
                sortConfirmation.close()
        }
    }

    CompanionPage {
        id: characterSettingsDialog
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        visible: root.currentPage === 5
        dark: root.editorDark
        character: characterController
        settings: characterSettings
        placement: characterWindow
        motion: companionMotion
        edits: companionEdits
        guideService: userGuides
        onOpenRequested: root.currentPage = 5
        onCloseRequested: if (root.currentPage === 5) root.currentPage = 0
    }

    SortConfirmationDialog {
        id: sortConfirmation
        controller: previewController
        busy: backgroundTasks.busy
        inlineConfirmation: root.inlineSortConfirmation
    }

    LegacyRuleEditorDialog {
        id: ruleDialog
        controller: ruleEditor
        busy: backgroundTasks.busy
        dark: root.editorDark
    }

    WatchSettingsPage {
        id: watchDialog
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        visible: root.currentPage === 3
        dark: root.editorDark
        controller: watchController
        onOpenRequested: root.currentPage = 3
        onCloseRequested: if (root.currentPage === 3) root.currentPage = 0
    }

    WatchProposalDialog {
        id: watchProposalDialog
        controller: watchController
        busy: backgroundTasks.busy
        onPendingAccepted: root.currentPage = 0
    }

    HistoryPage {
        id: historyDialog
        parent: extensionHost
        anchors.fill: parent
        visible: root.currentPage === 4
        dark: root.editorDark
        controller: historyController
        overlayHost: extensionWindow.contentItem
        busy: backgroundTasks.busy
        onOpenRequested: root.currentPage = 4
        onCloseRequested: if (root.currentPage === 4) root.currentPage = 0
    }

    RecoveryPage {
        id: recoveryDialog
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        visible: root.currentPage === 10
        dark: root.editorDark
        controller: recoveryController
        overlayHost: extensionWindow.contentItem
        busy: backgroundTasks.busy
        inspecting: root.inlineRecoveryInspection
        onOpenRequested: root.currentPage = 10
        onCloseRequested: if (root.currentPage === 10) root.currentPage = 0
    }
}

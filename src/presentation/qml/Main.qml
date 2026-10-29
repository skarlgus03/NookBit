import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    font.pixelSize: UiMetrics.textBody
    readonly property bool organizerAvailable: previewController !== null
    readonly property bool workBusy: backgroundTasks ? backgroundTasks.busy : false
    readonly property bool fileWorkActive: backgroundTasks ? backgroundTasks.fileWorkActive : false
    readonly property bool confirmationPending: previewController ? previewController.awaitingConfirmation : false
    // 트레이 종료에서도 마지막 캐릭터 위치와 열린 페이지 크기를 저장하는 함수.
    function commitCharacterPosition() { desktopUiHost.commitState(); extensionWindow.commitSize() }
    // 닫기는 트레이에 숨기고 프로세스 종료 여부는 별도 정책에 맡기는 함수.
    onClosing: function(close) {
        close.accepted = false
        root.commitCharacterPosition()
        appLifecycle.closeWindow()
    }
    RegisteredDesktopHost {
        id: desktopUiHost
        contribution: companionDesktop
        dark: root.editorDark
        attentionActionAvailable: !!notifications && !!watchController && notifications.activeTypes.includes("file.offer") && watchController.proposalVisible && !root.workBusy
        // 알림의 실제 확인 동작은 기능 사이 연결을 맡는 호스트에서 선택한다.
        onAttentionRequested: {
            if (!attentionActionAvailable) return
            appLifecycle.openWindow()
            root.openFeatureDialog("watch.proposal")
        }
        // 등록되지 않은 설정 페이지는 열지 않고 정상 페이지를 메인 창과 함께 복원한다.
        onPageRequested: function(pageId) {
            const page = uiPages.pageId(pageId)
            if (page === 0) return
            appLifecycle.openWindow()
            root.currentPage = page
        }
    }
    readonly property var operationProgressPopup: operationProgressHost.item
    Loader {
        id: operationProgressHost
        active: root.organizerAvailable
        sourceComponent: Component {
            OperationProgressPopup {
                parent: extensionWindow.contentItem
                progress: backgroundTasks
                dark: root.editorDark
                visible: root.workBusy && extensionWindow.visible && !root.inlineRecoveryInspection
                         && !(root.currentPage === 7 && fileSortPane && fileSortPane.workflowStep === 3)
                x: (parent.width - width) / 2
                y: extensionHost.y + (extensionHost.height - height) / 2
            }
        }
    }
    property int currentPage: 0
    readonly property var hostPageTitles: ({8: qsTr("설정"), 9: qsTr("개발 도구"), 12: qsTr("모듈 관리")})
    // 호스트 페이지와 현재 등록된 모듈 페이지만 열 수 있는지 확인하는 함수.
    function pageAvailable(page) { return page === 0 || page === 8 || page === 12 || (page === 9 && organizerAvailable) || uiPages.contains(page) }
    // 복구 목록의 읽기 전용 검사는 전체 오버레이 대신 해당 페이지 안에서 표시한다.
    readonly property bool inlineRecoveryInspection: currentPage === 10 && root.workBusy && !root.fileWorkActive
    readonly property bool inlineSortConfirmation: currentPage === 7 && !!fileSortPane && fileSortPane.manualSortRequested
    // 확인을 인라인 화면으로 넘길 때 기존 대화상자를 닫되 승인 대기는 취소하지 않는 함수.
    onInlineSortConfirmationChanged: if (inlineSortConfirmation && sortConfirmation && sortConfirmation.visible) sortConfirmation.close()
    readonly property int homeWidth: 380
    // 본체 크기는 유지하고 별도 연결 창의 페이지만 전환하는 함수.
    onCurrentPageChanged: {
        // 예전 독립 미리보기 번호는 파일 정리로 돌려 빈 페이지를 만들지 않는다.
        if (currentPage === 2) { currentPage = 7; return }
        if (!pageAvailable(currentPage)) currentPage = 0
    }
    Connections {
        target: uiPages
        // 없어진 페이지 번호로 빈 서브창이 남지 않게 홈으로 복귀하는 함수.
        function onPagesChanged() { if (!root.pageAvailable(root.currentPage)) root.currentPage = 0 }
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

    // 기능 화면이 요청한 제안 확인만 열고 파일 실행이나 승인을 시작하지 않는 함수.
    function openFeatureDialog(dialogId) {
        if (dialogId !== "watch.proposal" || !watchController || !watchController.proposalVisible || workBusy) return
        appLifecycle.openWindow()
        watchProposalDialog.open()
    }
    // 감시 입력 초안의 복원은 페이지에 맡기고 해당 화면을 여는 함수.
    function openWatchSettings() { openPage(uiPages.pageId("watch.settings")) }
    // 복구 화면을 먼저 연 뒤 파일을 바꾸지 않는 목록 검사를 요청하는 함수.
    function openRecovery() { openPage(uiPages.pageId("organizer.recovery")) }

    // 로더가 보관 중인 등록 페이지를 찾고 일반 전환에서는 다시 생성하지 않는 함수.
    function registeredPage(page) {
        if (!registeredPages) return null
        for (let i = 0; i < registeredPages.count; ++i) {
            const host = registeredPages.itemAt(i)
            if (host && host.registration.page === page) return host.item
        }
        return null
    }
    readonly property var fileSortPane: registeredPage(7)
    // 등록 화면은 자체 준비 동작을 거쳐 열고 나머지 호스트 화면으로도 이동하는 함수.
    function openPage(page) {
        if (!pageAvailable(page)) return
        const item = registeredPage(page)
        if (item) item.open()
        else currentPage = page
    }
    // 기록 조회 준비는 등록된 기록 화면에 맡기고 그 화면을 요청하는 함수.
    function openHistory() { openPage(uiPages.pageId("organizer.history")) }
    // 바탕화면 설정 요청도 안정적인 등록 이름으로 전달하는 함수.
    function openCharacterSettings() {
        const page = uiPages.pageId("companion.settings")
        if (page !== 0) openPage(page)
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
        pendingCount: pendingFiles ? pendingFiles.count : 0
        sortingAvailable: root.organizerAvailable
        companionAvailable: desktopUiHost.item !== null
        watchEnabled: watchController ? watchController.enabled : false
        watchFolder: watchController ? watchController.folder : ""
        companionDescriptor: desktopUiHost.summary
        registeredPages: uiPages.pages
        navigationEnabled: !root.workBusy && !root.confirmationPending
        currentPage: root.currentPage
        width: root.homeWidth
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        dark: root.editorDark
        // 선택한 페이지를 본체와 별개인 연결 창에 표시하는 함수.
        onPageRequested: function(page) { root.openPage(page) }
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
                uiSettings.rememberExtensionSize(sizeKey(activePage), width, height)
        }
        // 설정 안의 감시 화면은 설정 창의 크기를 공유하는 함수.
        function sizeKey(page) { return uiPages.page(page).host === "settingsDetail" ? 8 : page }
        // 이전 페이지 크기를 보존한 뒤 새 페이지의 기본값 또는 마지막 크기를 복원하는 함수.
        function activatePage(page) {
            if (page > 0 && activePage > 0 && sizeKey(page) === sizeKey(activePage)) {
                activePage = page
                return
            }
            commitSize()
            activePage = page
            if (page === 0) return
            const spec = UiMetrics.extensionDefaults[sizeKey(page)]
            if (!spec) return
            restoringSize = true
            minimumWidth = spec.minWidth
            minimumHeight = spec.minHeight
            const saved = uiSettings.extensionSize(sizeKey(page), Qt.size(spec.width, spec.height))
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
                text: uiPages.page(root.currentPage).title || root.hostPageTitles[root.currentPage] || ""
                color: root.theme.text
                font.pixelSize: UiMetrics.textWindowTitle; font.bold: true
            }
            BackButton { description: qsTr("설정으로 돌아가기"); enabled: !root.workBusy; visible: uiPages.page(root.currentPage).host === "settingsDetail" || [6, 10, 11].indexOf(root.currentPage) >= 0; onClicked: root.currentPage = 8 }
            NavigationButton { objectName: "extensionCloseButton"; text: qsTr("접기 ×"); enabled: !root.workBusy; onClicked: root.currentPage = 0 }
        }
    }
    Item {
        id: extensionHost
        enabled: (!root.workBusy || (root.currentPage === 7 && fileSortPane && fileSortPane.workflowStep === 3)) && (!root.confirmationPending || root.inlineSortConfirmation)
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
        visible: !!operationProgressPopup && operationProgressPopup.visible
        color: root.theme.inspectionVeil
        opacity: UiMetrics.operationVeilOpacity
        z: 90
    }
    Shortcut {
        sequence: "Ctrl+Shift+D"
        context: Qt.ApplicationShortcut
        enabled: root.organizerAvailable && !root.workBusy && !root.confirmationPending
        // 사용자 화면과 분리된 개발 도구 페이지를 여는 함수.
        onActivated: root.currentPage = root.currentPage === 9 ? 0 : 9
    }
    SettingsWorkflow {
        objectName: "settingsWorkflow"
        parent: extensionHost
        anchors.fill: parent
        visible: root.currentPage === 8 || root.currentPage === detailPage.page
        active: visible
        detailPage: {
            for (const entry of uiPages.pages) if (entry.id === "watch.settings") return entry
            return ({})
        }
        detailOpen: root.currentPage === detailPage.page
        guideService: userGuides
        overlayHost: extensionWindow.contentItem
        dark: root.editorDark
        settings: uiSettings
        organizerContribution: organizerSettings
        startupMessages: featureStartupMessages
        applicationVersion: Qt.application.version
        watchContribution: watchSettingsEntry
        onSettingsRequested: root.currentPage = 8
        onRouteRequested: function(route) { root.openPage(route) }
        onDialogRequested: function(dialogId) { root.openFeatureDialog(dialogId) }
    }

    ModuleManagementPage {
        parent: extensionHost
        anchors.fill: parent
        visible: root.currentPage === 12
        controller: featureSettings
        dark: root.editorDark
    }

    readonly property var sortConfirmation: organizerHostUi.item ? organizerHostUi.item.confirmation : null
    Loader {
        id: organizerHostUi
        active: root.organizerAvailable
        sourceComponent: Component {
            Item {
                property alias confirmation: legacyConfirmation
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
                    bubbleTestAvailable: desktopUiHost.item !== null
                    onBubbleTestRequested: desktopUiHost.preview()
                    onLegacyEditorRequested: {
                        root.openPage(uiPages.pageId("organizer.rules"))
                        const page = root.registeredPage(uiPages.pageId("organizer.rules"))
                        if (page) page.openLegacyEditor()
                    }
                    onRulesRequested: root.openPage(uiPages.pageId("organizer.rules"))
                    onRecoveryRequested: root.openRecovery()
                }

                Connections {
                    target: previewController
                    // 목록·규칙·설정 변경으로 확인이 무효화되면 창도 닫는 함수.
                    function onPreviewChanged() {
                        if (root.confirmationPending && !root.inlineSortConfirmation && sortConfirmation && !sortConfirmation.visible)
                            sortConfirmation.open()
                        else if (!root.confirmationPending && sortConfirmation && sortConfirmation.visible)
                            sortConfirmation.close()
                    }
                }

                SortConfirmationDialog {
                    id: legacyConfirmation
                    parent: root.contentItem
                    controller: previewController
                    busy: root.workBusy
                    inlineConfirmation: root.inlineSortConfirmation
                }
            }
        }
    }
    Repeater {
        id: registeredPages
        parent: extensionHost
        model: uiPages.extensionPages
        delegate: RegisteredPageHost {
            required property var modelData
            registration: modelData
            anchors.fill: parent
            selected: root.currentPage === modelData.page
            dark: root.editorDark
            guideService: userGuides
            overlayHost: extensionWindow.contentItem
            onDialogRequested: function(dialogId) { root.openFeatureDialog(dialogId) }
            onNavigationRequested: function(page) { root.openPage(page) }
            onPageRequested: function(page) { root.currentPage = page }
            onCloseRequested: function(page) { if (root.currentPage === page) root.currentPage = 0 }
        }
    }

    RegisteredDialogHost {
        id: watchProposalDialog
        contribution: watchProposalUi
        overlayHost: root.contentItem
        busy: root.workBusy
        dark: root.editorDark
        onAccepted: root.currentPage = 0
    }


}

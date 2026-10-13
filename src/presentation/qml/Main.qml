import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Dialogs as NativeDialogs

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
        userScale: characterSettings.userScale
        actionAvailable: watchController.proposalVisible && !backgroundTasks.busy
        onActionRequested: {
            if (!watchController.proposalVisible || backgroundTasks.busy) return
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
    Popup {
        id: operationProgressPopup
        objectName: "operationProgressPopup"
        parent: extensionWindow.contentItem
        visible: backgroundTasks.busy && extensionWindow.visible && !(root.currentPage === 7 && fileSortPane.workflowStep === 3)
        modal: false
        dim: false
        focus: false
        closePolicy: Popup.NoAutoClose
        x: (parent.width - width) / 2
        y: extensionHost.y + (extensionHost.height - height) / 2
        width: Math.min(UiMetrics.operationPopupWidth, parent.width - UiMetrics.pageInset * 2)
        height: UiMetrics.operationPopupHeight
        padding: UiMetrics.spaceCompact
        background: Rectangle {
            radius: UiMetrics.radiusCard
            color: root.theme.surface
            border.color: root.theme.border
        }
        contentItem: ColumnLayout {
            spacing: UiMetrics.spaceSmall
            Label {
                text: backgroundTasks.stageLabel
                color: root.editorDark ? "#edf2f8" : "#253428"
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Label {
                text: backgroundTasks.currentFileName
                visible: text.length > 0
                color: root.editorDark ? "#edf2f8" : "#253428"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
            }
            Label {
                text: backgroundTasks.filePosition
                visible: text.length > 0
                color: root.editorDark ? "#b7c6dc" : "#4b6153"
                Layout.fillWidth: true
            }
            ProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: backgroundTasks.byteFraction
                indeterminate: !backgroundTasks.byteProgressKnown
                Accessible.name: backgroundTasks.stageLabel
            }
            Label {
                text: backgroundTasks.byteProgressText
                visible: backgroundTasks.byteProgressKnown
                color: root.editorDark ? "#edf2f8" : "#253428"
                Layout.fillWidth: true
            }
            Label {
                text: "복사 후 검증·확정까지 마쳐야 완료돼요."
                wrapMode: Text.WordWrap
                color: root.editorDark ? "#b7c6dc" : "#4b6153"
                font.pixelSize: UiMetrics.textBody
                Layout.fillWidth: true
            }
        }
    }
    property int currentPage: 0
    readonly property bool inlineSortConfirmation: currentPage === 7 && fileSortPane.manualSortRequested
    // 확인을 인라인 화면으로 넘길 때 기존 대화상자를 닫되 승인 대기는 취소하지 않는 함수.
    onInlineSortConfirmationChanged: if (inlineSortConfirmation && sortConfirmation.visible) sortConfirmation.close()
    readonly property int homeWidth: 380
    // 본체 크기는 유지하고 별도 연결 창의 페이지만 전환하는 함수.
    onCurrentPageChanged: {
        // 예전 독립 미리보기 번호는 파일 정리로 돌려 빈 페이지를 만들지 않는다.
        if (currentPage === 2) { currentPage = 7; return }
        if (currentPage !== 3) watchDialog.close()
        if (currentPage !== 4) { historyDialog.close(); undoHistoryDialog.close() }
        if (currentPage !== 5) characterSettingsDialog.close()
        if (currentPage !== 10 && recoveryController.awaitingRecoveryConfirmation)
            recoveryController.cancelRecoveryChoice()
    }
    property string selectedRulePath: ""
    property var selectedRuleDetail: ({})
    property var collapsedRulePaths: ({})
    property bool ruleFormDirty: false
    property bool globalFormDirty: false
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

    // 저장된 감시 설정으로 입력창을 채운 뒤 별도 창을 여는 함수.
    function openWatchSettings() {
        currentPage = 3
        watchFolderField.text = watchController.folder
        watchEnabledCheck.checked = watchController.enabled
        watchRecursiveCheck.checked = watchController.recursive
        watchIntervalSpin.value = watchController.rescanIntervalMinutes
        watchCombination.currentIndex = watchController.combineAll ? 1 : 0
        watchCountCheck.checked = watchController.countEnabled
        watchCountSpin.value = watchController.minimumFileCount
        watchSizeCheck.checked = watchController.sizeEnabled
        watchSizeSpin.value = watchController.minimumTotalSize
        watchSizeUnit.currentIndex = watchSizeUnit.indexOfValue(watchController.totalSizeUnit)
        watchAgeCheck.checked = watchController.ageEnabled
        watchAgeSpin.value = watchController.minimumAgeMinutes
        watchExcludeField.text = watchController.excludePatterns
        watchDialog.open()
    }

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

    // 탐색 버튼은 선택 상태만 배경에 표시하고 마우스 강조는 테두리로 나타내는 버튼.
    component NavigationButton: T.Button {
        id: navButton
        padding: UiMetrics.buttonPadding
        implicitWidth: Math.max(UiMetrics.buttonMinWidth, contentItem.implicitWidth + leftPadding + rightPadding)
        implicitHeight: Math.max(UiMetrics.controlHeight, contentItem.implicitHeight + topPadding + bottomPadding)
        contentItem: Text {
            text: navButton.text
            font: navButton.font
            color: root.theme.navigationText
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: UiMetrics.radiusControl
            color: navButton.highlighted ? root.theme.navigationSelected : root.theme.navigationButton
            border.color: navButton.hovered || navButton.visualFocus || navButton.down
                          ? root.theme.navigationFocus : root.theme.navigationBorder
            border.width: navButton.hovered || navButton.visualFocus || navButton.down ? UiMetrics.borderActive : UiMetrics.borderNormal
        }
    }

    // 설정 본문은 팝업 오버레이 없이 확장 창 안의 일반 페이지로 표시한다.
    component EmbeddedPage: Pane {
        required property int pageIndex
        readonly property bool opened: visible
        visible: root.currentPage === pageIndex
        background: null
        padding: 0
        // 해당 페이지를 선택하는 함수.
        function open() { root.currentPage = pageIndex }
        // 자신이 선택되어 있을 때만 확장 창을 접는 함수.
        function close() { if (root.currentPage === pageIndex) root.currentPage = 0 }
    }

    // 접힌 부모 아래의 노드를 화면에서 숨기는 함수.
    function ruleVisible(path) {
        const parts = path.split("/")
        for (let index = 1; index < parts.length; ++index) {
            if (collapsedRulePaths[parts.slice(0, index).join("/")])
                return false
        }
        return true
    }

    // 선택한 가지의 접힘 상태를 바꾸는 함수.
    function toggleRule(path) {
        const next = Object.assign({}, collapsedRulePaths)
        next[path] = !next[path]
        collapsedRulePaths = next
    }

    // 입력 중인 규칙을 초안에 반영하고 유효하지 않으면 전환을 막는 함수.
    function commitRuleForm() {
        if (!ruleFormDirty || selectedRulePath.length === 0)
            return true
        const saved = ruleEditor.edit(selectedRulePath, ruleIdField.text,
                                      ruleEnabledCheck.checked, ruleConditionType.currentValue,
                                      ruleConditionComparison.currentValue, ruleConditionValue.text,
                                      ruleSizeUnit.currentValue, ruleDestinationField.text)
        if (saved)
            ruleFormDirty = false
        return saved
    }

    // 선택한 규칙의 초안 값을 편집 입력창에 채우는 함수.
    function selectRule(path) {
        selectedRulePath = path
        selectedRuleDetail = ruleEditor.detail(path)
        ruleIdField.text = selectedRuleDetail.id || ""
        ruleEnabledCheck.checked = selectedRuleDetail.enabled === true
        ruleConditionType.currentIndex = Math.max(0, ruleConditionType.indexOfValue(selectedRuleDetail.type || "extension"))
        ruleConditionComparison.currentIndex = Math.max(0, ruleConditionComparison.indexOfValue(selectedRuleDetail.comparison || "greaterThan"))
        ruleConditionValue.text = selectedRuleDetail.value === undefined ? "" : String(selectedRuleDetail.value)
        ruleSizeUnit.currentIndex = Math.max(0, ruleSizeUnit.indexOfValue(selectedRuleDetail.sizeUnit || "MiB"))
        ruleDestinationField.text = selectedRuleDetail.destination || ""
        ruleFormDirty = false
    }
    width: homeWidth
    height: Math.min(540, Screen.desktopAvailableHeight - 60)
    minimumWidth: homeWidth
    maximumWidth: homeWidth
    minimumHeight: 420
    visible: true
    title: qsTr("NookBit")
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint

    CompactHome {
        id: compactHome
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
        color: root.editorDark ? "#161a22" : "#ffffff"
        border.color: root.editorDark ? "#3d4757" : "#e1e6ed"
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
                text: ["", qsTr("분류 규칙"), qsTr("분류 결과"), qsTr("폴더 감시 설정"), qsTr("작업 기록"), qsTr("캐릭터 설정"), qsTr("이름 충돌 설정"), qsTr("파일 정리"), qsTr("설정"), qsTr("개발 도구"), qsTr("중단 작업 확인")][root.currentPage]
                color: root.editorDark ? "#edf2f8" : "#273344"
                font.pixelSize: UiMetrics.textWindowTitle; font.bold: true
            }
            BackButton { description: qsTr("설정으로 돌아가기"); enabled: !backgroundTasks.busy; visible: [3, 5, 6, 10].indexOf(root.currentPage) >= 0; onClicked: root.currentPage = 8 }
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
    ColumnLayout {
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        visible: root.currentPage === 6
        Label { text: qsTr("같은 이름의 파일이 이미 있으면"); font.bold: true }
        AppComboBox {
            textRole: "label"; valueRole: "key"
            model: [{label: qsTr("이름 변경"), key: "rename"}, {label: qsTr("건너뛰기"), key: "skip"}, {label: qsTr("작업 중단"), key: "abort"}]
            currentIndex: previewController.collisionPolicy === "skip" ? 1 : previewController.collisionPolicy === "abort" ? 2 : 0
            onActivated: previewController.collisionPolicy = currentValue
        }
        Label { Layout.fillWidth: true; text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정이 우선합니다."); wrapMode: Text.Wrap }
        Item { Layout.fillHeight: true }
    }

    FileSortPage {
        id: fileSortPane
        parent: extensionHost
        anchors.fill: parent
        visible: root.currentPage === 7
        dark: root.editorDark
        // 파일 정리에서 요청한 규칙 편집 페이지를 연결 창에 표시하는 함수.
        onPageRequested: function(page) { root.currentPage = page }
        onRecoveryRequested: { recoveryController.refreshRecoveryCases(); recoveryDialog.open() }
    }
    ScrollView {
        id: settingsScroll
        objectName: "settingsPage"
        parent: extensionHost
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        visible: root.currentPage === 8
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: settingsScroll.availableWidth
            spacing: UiMetrics.settingsSectionSpacing
            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("화면")
                dark: root.editorDark
                Switch {
                    objectName: "globalDarkModeSwitch"
                    text: qsTr("앱 전체 다크모드")
                    font.weight: UiMetrics.settingsLabelWeight
                    checked: uiSettings.darkMode
                    onToggled: uiSettings.darkMode = checked
                }
                Label { Layout.fillWidth: true; text: qsTr("테마는 자동 저장돼요."); wrapMode: Text.Wrap; color: root.mutedText }
            }
            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("기능 설정")
                dark: root.editorDark
                NavigationButton { Layout.fillWidth: true; font.weight: UiMetrics.settingsLabelWeight; text: qsTr("폴더 감시 설정  ›"); onClicked: root.openWatchSettings() }
                NavigationButton { Layout.fillWidth: true; font.weight: UiMetrics.settingsLabelWeight; text: qsTr("캐릭터 설정  ›"); onClicked: root.openCharacterSettings() }
                NavigationButton { Layout.fillWidth: true; font.weight: UiMetrics.settingsLabelWeight; text: qsTr("파일 이름 충돌 처리  ›"); onClicked: root.currentPage = 6 }
                NavigationButton { Layout.fillWidth: true; font.weight: UiMetrics.settingsLabelWeight; text: qsTr("중단 작업 확인"); visible: recoveryController.recoveryCases.length > 0; onClicked: { recoveryController.refreshRecoveryCases(); recoveryDialog.open() } }
            }
            SettingsSection {
                Layout.fillWidth: true
                title: qsTr("허용한 외부 목적지")
                dark: root.editorDark
                Label {
                    Layout.fillWidth: true
                    visible: destinationSettings.allowedRoots.length === 0
                    text: qsTr("없음 · 규칙 편집기의 목적지 설정에서 폴더를 허용할 수 있어요.")
                    color: root.mutedText
                    wrapMode: Text.Wrap
                }
                Repeater {
                    model: destinationSettings.allowedRoots
                    RowLayout {
                        required property string modelData
                        Layout.fillWidth: true
                        Label { Layout.fillWidth: true; font.weight: UiMetrics.settingsLabelWeight; text: modelData; elide: Text.ElideMiddle; color: root.primaryText }
                        AppButton { font.weight: UiMetrics.settingsLabelWeight; text: qsTr("허용 해제"); onClicked: destinationSettings.revokeFolder(modelData) }
                    }
                }
            }
        }
    }

    ScrollView {
        id: sortingScroll
        parent: extensionHost
        anchors.fill: parent
        anchors.leftMargin: 18
        anchors.rightMargin: 28
        anchors.topMargin: 10
        anchors.bottomMargin: 28
        visible: root.currentPage === 9
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: sortingScroll.availableWidth
            spacing: UiMetrics.spaceSection

            ColumnLayout {
                objectName: "developerTools"
                Layout.fillWidth: true
                visible: root.currentPage === 9
                Label { text: qsTr("개발용 기존 정리 화면 · Ctrl+Shift+D로 접기"); font.bold: true }
                AppButton { text: qsTr("말풍선 배치 테스트"); onClicked: { characterWindow.setShown(true); desktopPet.testBubble() } }
                AppButton { text: qsTr("기존 규칙 폼 열기"); onClicked: ruleDialog.open() }

            Label {
                text: qsTr("파일 정리")
                font.pixelSize: UiMetrics.textPageTitle
                font.bold: true
                color: root.primaryText
            }

            Label {
                text: qsTr("정리할 파일을 먼저 대기 목록에 담아 주세요.")
                font.pixelSize: UiMetrics.textEmphasis
                color: root.mutedText
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("현재 적용 중인 규칙: ") + previewController.activeRuleLabel
                    color: root.editorDark ? "#b6d8c5" : "#426349"
                    elide: Text.ElideMiddle
                }
                AppButton {
                    text: qsTr("규칙 편집기로")
                    onClicked: root.currentPage = 1
                }
            }

            Label {
                Layout.fillWidth: true
                visible: recoveryController.recoveryNotice.length > 0
                text: recoveryController.recoveryNotice
                color: root.editorDark ? "#e5bf85" : "#9b5c23"
                wrapMode: Text.WordWrap
            }

            AppButton {
                visible: recoveryController.recoveryCases.length > 0
                text: qsTr("중단 작업 확인")
                onClicked: {
                    recoveryController.refreshRecoveryCases()
                    recoveryDialog.open()
                }
            }

            Rectangle {
                id: dropZone
                property bool dragging: false
                Layout.fillWidth: true
                Layout.preferredHeight: 120
                radius: 18
                color: dragging ? (root.editorDark ? "#304438" : "#e8f4e5") : root.surfaceColor
                border.width: dragging ? 2 : 1
                border.color: dragging ? "#4d8f57" : root.borderColor

                Column {
                    anchors.centerIn: parent
                    spacing: UiMetrics.spaceControl

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("파일을 여기로 끌어놓으세요")
                        font.pixelSize: UiMetrics.textSectionTitle
                        font.bold: true
                        color: root.primaryText
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("원본 파일은 이동하거나 복사하지 않습니다.")
                        color: root.mutedText
                    }
                }

                DropArea {
                    anchors.fill: parent
                    onEntered: (drag) => {
                        const canRegister = drag.hasUrls && (drag.supportedActions & Qt.CopyAction)
                        dropZone.dragging = canRegister
                        if (canRegister)
                            drag.accept(Qt.CopyAction)
                        else
                            drag.accepted = false
                    }
                    onExited: dropZone.dragging = false
                    onDropped: (drop) => {
                        dropZone.dragging = false
                        if (drop.hasUrls && (drop.supportedActions & Qt.CopyAction)) {
                            for (const url of drop.urls)
                                pendingFiles.addUrl(url)
                            drop.accept(Qt.CopyAction)
                        } else {
                            drop.accepted = false
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true

                Label {
                    text: qsTr("1. 대기 목록")
                    font.pixelSize: UiMetrics.textSectionTitle
                    font.bold: true
                    color: root.primaryText
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%1개 대기 중").arg(pendingFiles.count)
                    color: root.mutedText
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 16
                color: root.surfaceColor
                border.color: root.borderColor

                Label {
                    anchors.centerIn: parent
                    visible: pendingFiles.count === 0
                    text: qsTr("아직 대기 중인 파일이 없어요.")
                    color: root.mutedText
                }

                ListView {
                    id: pendingList
                    anchors.fill: parent
                    anchors.margins: UiMetrics.spaceGroup
                    clip: true
                    spacing: UiMetrics.spaceExtraSmall
                    visible: pendingFiles.count > 0
                    model: pendingFiles

                    delegate: Rectangle {
                        required property int index
                        required property string fileName
                        required property string fullPath
                        required property bool available
                        width: pendingList.width
                        height: 70
                        radius: UiMetrics.radiusPanel
                        color: !available ? (root.editorDark ? "#4a3033" : "#fff0ec") : index % 2 === 0 ? (root.editorDark ? "#242d3b" : "#f7f9f6") : root.surfaceColor

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 12
                            spacing: UiMetrics.spaceGroup

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: UiMetrics.spaceExtraSmall
                                Label {
                                    text: available ? fileName : qsTr("[파일 없음] %1").arg(fileName)
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: fullPath
                                    color: root.mutedText
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            AppButton {
                                text: qsTr("목록에서 빼기")
                                onClicked: pendingFiles.removeAt(index)
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: pendingFiles.unavailableCount > 0
                text: qsTr("찾을 수 없는 파일 %1개가 있어요. 해당 항목을 목록에서 빼거나 파일을 원래 위치에 돌려놓아 주세요.")
                      .arg(pendingFiles.unavailableCount)
                color: root.editorDark ? "#ef9c90" : "#b44732"
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: pendingFiles.storageNotice.length > 0
                text: pendingFiles.storageNotice
                color: root.editorDark ? "#ef9c90" : "#b44732"
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                Label { text: qsTr("같은 이름의 파일이 있으면"); color: root.primaryText }
                AppComboBox {
                    textRole: "label"
                    valueRole: "key"
                    model: [
                        { label: qsTr("이름 변경"), key: "rename" },
                        { label: qsTr("건너뛰기"), key: "skip" },
                        { label: qsTr("작업 중단"), key: "abort" }
                    ]
                    currentIndex: previewController.collisionPolicy === "skip" ? 1
                                  : previewController.collisionPolicy === "abort" ? 2 : 0
                    onActivated: previewController.collisionPolicy = currentValue
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정을 우선해요.")
                    color: root.mutedText
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup

                Label {
                    text: qsTr("정리할 위치")
                    color: root.primaryText
                }
                AppTextField {
                    id: destinationField
                    Layout.fillWidth: true
                    text: previewController.defaultDestinationRoot
                    placeholderText: qsTr("정리할 폴더의 전체 경로")
                    onTextEdited: previewController.clear()
                }
                AppButton {
                    text: qsTr("미리보기 만들기")
                    enabled: pendingFiles.count > 0
                    onClicked: previewController.createPreview(destinationField.text)
                }
                AppButton {
                    text: qsTr("정리하기")
                    enabled: previewController.hasPreview && previewController.hasMovableSteps
                             && !previewController.awaitingConfirmation
                    onClicked: {
                        previewController.prepareSort()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup

                AppButton {
                    text: qsTr("마지막 이동 되돌리기")
                    enabled: previewController.canUndo
                    onClicked: previewController.undoLastMove()
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("중간에 실패하면 되돌리기를 시도하고, 불확실하면 확인을 요청해요.")
                    color: root.mutedText
                }
            }

            Label {
                Layout.fillWidth: true
                text: previewController.message.length > 0
                      ? previewController.message
                      : qsTr("저장된 규칙으로 미리보기를 만듭니다. 편집 중인 초안은 전체 저장 후 적용돼요.")
                color: root.editorDark ? "#b6d8c5" : "#426349"
                wrapMode: Text.WordWrap
            }

            }
            Label {
                text: qsTr("분류 미리보기")
                font.pixelSize: UiMetrics.textSectionTitle
                font.bold: true
                color: root.primaryText
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                radius: 16
                color: root.surfaceColor
                border.color: root.borderColor

                Label {
                    anchors.centerIn: parent
                    visible: !previewController.hasPreview
                    text: qsTr("미리보기를 만들면 예상 목적지가 여기에 표시됩니다.")
                    color: root.mutedText
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: UiMetrics.spaceGroup
                    visible: previewController.hasPreview
                    spacing: UiMetrics.spaceSmall

                    ListView {
                        id: previewList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: UiMetrics.spaceControl
                        model: previewController.outcomes
                        delegate: Rectangle {
                            required property var modelData
                            width: previewList.width
                            height: previewRow.implicitHeight + 20
                            radius: UiMetrics.radiusPanel
                            color: modelData.statusKey === "planned" ? (root.editorDark ? "#25382e" : "#f3f9f2") : (root.editorDark ? "#242d3b" : "#f7f7f4")
                            border.color: modelData.statusKey === "planned" ? (root.editorDark ? "#4e705a" : "#b9d8b9") : root.borderColor
                            ColumnLayout {
                                id: previewRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: UiMetrics.spaceCompact
                                spacing: UiMetrics.spaceExtraSmall
                                RowLayout {
                                    Layout.fillWidth: true
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.fileName
                                        font.bold: true
                                        elide: Text.ElideMiddle
                                    }
                                    Label {
                                        text: modelData.status
                                        font.bold: true
                                        color: modelData.statusKey === "planned" ? (root.editorDark ? "#92c69f" : "#327542") : (root.editorDark ? "#e5bf85" : "#805b2c")
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.reason
                                    color: root.primaryText
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("원본: ") + modelData.source
                                    color: root.mutedText
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.destination.length > 0
                                          ? qsTr("예상 목적지: ") + modelData.destination
                                          : qsTr("이동하지 않음 · 원본 유지")
                                    color: root.mutedText
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: previewController.outcomes.length === 0
                        text: qsTr("표시할 파일별 검사 결과가 없어요. 위의 경고를 확인해 주세요.")
                        color: root.mutedText
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: previewController.warnings.length > 0
                        text: qsTr("추가 경고: ") + previewController.warnings.join("\n")
                        color: root.editorDark ? "#e5bf85" : "#9b5c23"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: pendingFiles.lastMessage.length > 0
                      ? pendingFiles.lastMessage
                      : qsTr("승인 전에는 파일을 이동하지 않습니다.")
                color: root.editorDark ? "#b6d8c5" : "#426349"
                wrapMode: Text.WordWrap
            }
        }
    }

    RuleEditorPage {
        id: ruleEditorPage
        parent: extensionHost
        objectName: "ruleEditorPage"
        oldEditorFormDirty: root.ruleFormDirty || root.globalFormDirty
        anchors.fill: parent
        anchors.leftMargin: 0
        anchors.topMargin: 0
        visible: root.currentPage === 1
        darkMode: root.editorDark
        onThemeChangeRequested: uiSettings.darkMode = !uiSettings.darkMode
        onOpenCurrentEditor: ruleDialog.open()
        onEditorDraftReplaced: {
            root.selectedRulePath = ""
            root.selectedRuleDetail = ({})
            root.ruleFormDirty = false
            root.globalFormDirty = false
            root.collapsedRulePaths = ({})
            globalDestinationField.text = ruleEditor.globalDestination
        }
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

    NativeDialogs.FolderDialog {
        id: imagePackFolderDialog
        title: qsTr("imagepack.json이 들어 있는 캐릭터 팩 폴더 선택")
        onAccepted: characterSettings.importFolder(selectedFolder)
    }

    GuideDownload {
        id: characterGuideDownload
        objectName: "characterGuideDownload"
        parent: extensionHost
        guideKey: "character"
    }

    EmbeddedPage {
        id: characterSettingsDialog
        objectName: "characterSettingsDialog"
        parent: extensionHost
        pageIndex: 5
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceGroup
            Label { text: qsTr("캐릭터 팩"); font.bold: true }
            AppButton {
                objectName: "characterUserGuideButton"
                text: qsTr("제작 가이드 저장…")
                onClicked: characterGuideDownload.saveDocument()
            }
            Label {
                Layout.fillWidth: true
                visible: characterGuideDownload.status !== ""
                text: characterGuideDownload.status
                wrapMode: Text.Wrap
            }
            CheckBox {
                text: qsTr("캐릭터를 앱 밖에 표시")
                checked: characterWindow.shown
                onToggled: characterWindow.setShown(checked)
            }
            AppButton { text: qsTr("캐릭터 위치 되돌리기"); onClicked: characterWindow.resetPosition() }
            Label { text: qsTr("왼쪽 드래그: 이동 · 우클릭: 설정 · ×: 숨기기"); font.pixelSize: UiMetrics.textBody }
            AppComboBox {
                id: characterPackChoice
                Layout.fillWidth: true
                model: characterSettings.packs
                textRole: "label"
                valueRole: "key"
                currentIndex: {
                    const items = characterSettings.packs
                    for (let i = 0; i < items.length; ++i)
                        if (items[i].key === characterSettings.selectedKey) return i
                    return -1
                }
                enabled: !characterSettings.busy
                onActivated: characterSettings.selectPack(currentValue)
            }
            AppButton {
                text: qsTr("팩 폴더 가져오기…")
                enabled: !characterSettings.busy
                onClicked: imagePackFolderDialog.open()
            }
            Label { text: qsTr("표시 크기: %1%").arg(Math.round(characterSettings.userScale * 100)) }
            Slider {
                Layout.fillWidth: true
                from: 0.1; to: 2.0; stepSize: 0.05
                value: characterSettings.userScale
                onMoved: characterSettings.setUserScale(value)
            }
            AppButton { text: qsTr("크기 기본값 (50%)"); onClicked: characterSettings.setUserScale(0.5) }
            Label {
                Layout.fillWidth: true
                text: characterSettings.status
                wrapMode: Text.Wrap
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("선택과 크기는 자동 저장돼요. 팩 원본은 변경하지 않아요.")
                wrapMode: Text.Wrap
                font.pixelSize: UiMetrics.textBody
            }
        }
    }

    Dialog {
        enabled: !backgroundTasks.busy
        id: sortConfirmation
        objectName: "legacySortConfirmation"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 760)
        height: Math.min(root.height - 32, 520)
        modal: true
        title: qsTr("파일 이동 최종 확인")
        onClosed: if (!root.inlineSortConfirmation) previewController.cancelSort()

        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceGroup
            Label {
                Layout.fillWidth: true
                text: qsTr("아래 %1개 파일을 이동할까요? 미분류·제외·건너뜀 파일은 그대로 둡니다.")
                      .arg(previewController.confirmationSteps.length)
                wrapMode: Text.WordWrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: UiMetrics.spaceCompact
                model: previewController.confirmationSteps
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    required property var modelData
                    width: ListView.view.width
                    spacing: UiMetrics.spaceExtraSmall
                    Label { width: parent.width; text: modelData.source; wrapMode: Text.WrapAnywhere }
                    Label { width: parent.width; text: qsTr("→ %1").arg(modelData.destination); wrapMode: Text.WrapAnywhere }
                }
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("확인 후에도 파일과 규칙을 다시 검사해요. 변경이 있으면 새 미리보기가 필요해요.")
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                AppButton { text: qsTr("취소"); onClicked: sortConfirmation.close() }
                AppButton {
                    text: qsTr("확인하고 이동")
                    enabled: previewController.awaitingConfirmation
                    onClicked: previewController.confirmSort()
                }
            }
        }
    }

    Dialog {
        enabled: !backgroundTasks.busy
        id: ruleDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 820)
        height: Math.min(root.height - 32, 660)
        modal: true
        title: qsTr("분류 규칙 편집")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceControl

            Label {
                Layout.fillWidth: true
                text: qsTr("위에서부터 검사하고, 한 가지가 맞으면 그 아래로 내려갑니다. 목적지는 정리 기준 폴더 안의 상대 경로입니다.")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: ruleEditor.message
                color: ruleEditor.dirty ? (root.editorDark ? "#e5bf85" : "#9b5c23") : (root.editorDark ? "#b6d8c5" : "#426349")
                wrapMode: Text.WordWrap
            }
            AppButton {
                visible: ruleEditor.legacyRules
                text: qsTr("기존 규칙 백업 후 새 형식으로 변환")
                onClicked: ruleEditor.migrateLegacyRules()
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    text: qsTr("루트 규칙 추가")
                    onClicked: {
                        if (root.commitRuleForm() && ruleEditor.add("")) {
                            root.collapsedRulePaths = ({})
                            root.selectRule("")
                        }
                    }
                }
                AppButton {
                    text: qsTr("루트 '그 외' 추가")
                    onClicked: {
                        if (root.commitRuleForm() && ruleEditor.addOtherwise("")) {
                            root.collapsedRulePaths = ({})
                            root.selectRule("")
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                AppButton {
                    text: qsTr("저장 전 변경 버리기")
                    enabled: ruleEditor.dirty || root.ruleFormDirty || root.globalFormDirty
                    onClicked: {
                        ruleEditor.reload()
                        root.selectRule("")
                        globalDestinationField.text = ruleEditor.globalDestination
                        root.globalFormDirty = false
                    }
                }
                AppButton {
                    text: qsTr("규칙 저장")
                    enabled: ruleEditor.dirty || root.ruleFormDirty || root.globalFormDirty
                    onClicked: {
                        if (!root.commitRuleForm())
                            return
                        if (root.globalFormDirty)
                            ruleEditor.setGlobalDestination(globalDestinationField.text)
                        if (ruleEditor.save()) {
                            root.ruleFormDirty = false
                            root.globalFormDirty = false
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("전체 예외 목적지") }
                AppTextField {
                    id: globalDestinationField
                    Layout.fillWidth: true
                    text: ruleEditor.globalDestination
                    placeholderText: qsTr("비우면 미분류")
                    onTextEdited: root.globalFormDirty = true
                }
                AppButton {
                    text: qsTr("적용")
                    onClicked: {
                        ruleEditor.setGlobalDestination(globalDestinationField.text)
                        root.globalFormDirty = false
                    }
                }
            }
            SplitView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                orientation: Qt.Horizontal

                ScrollView {
                    SplitView.preferredWidth: 350
                    SplitView.minimumWidth: 220
                    clip: true
                    Column {
                        width: parent.width
                        spacing: UiMetrics.spaceExtraSmall
                        Repeater {
                            model: ruleEditor.nodes
                            delegate: Row {
                                required property var modelData
                                width: parent.width
                                visible: root.ruleVisible(modelData.path)
                                height: visible ? 36 : 0
                                spacing: UiMetrics.spaceTiny
                                Item { width: modelData.depth * 16; height: 1 }
                                AppButton {
                                    width: 32
                                    height: 34
                                    flat: true
                                    visible: modelData.childrenCount > 0
                                    text: root.collapsedRulePaths[modelData.path] ? "▸" : "▾"
                                    onClicked: root.toggleRule(modelData.path)
                                }
                                AppButton {
                                    width: parent.width - modelData.depth * 16 - 36
                                    height: 34
                                    flat: true
                                    highlighted: root.selectedRulePath === modelData.path
                                    text: (modelData.enabled ? "" : "[꺼짐] ") + modelData.title +
                                          (modelData.exclude ? qsTr("  → 정리 제외")
                                           : modelData.destination ? "  → " + modelData.destination : "")
                                    onClicked: {
                                        if (root.commitRuleForm()) root.selectRule(modelData.path)
                                    }
                                }
                            }
                        }
                    }
                }

                ScrollView {
                    SplitView.fillWidth: true
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: UiMetrics.spaceControl
                        Label {
                            text: root.selectedRulePath.length > 0 ? qsTr("선택한 규칙") : qsTr("왼쪽에서 규칙을 선택하세요")
                            font.bold: true
                        }
                        Label { text: qsTr("규칙 ID (영문·숫자)") }
                        AppTextField {
                            id: ruleIdField
                            Layout.fillWidth: true
                            enabled: root.selectedRulePath.length > 0
                            onTextEdited: root.ruleFormDirty = true
                        }
                        CheckBox {
                            id: ruleEnabledCheck
                            text: qsTr("규칙 사용")
                            enabled: root.selectedRulePath.length > 0
                            onToggled: root.ruleFormDirty = true
                        }
                        Label { text: qsTr("분류 조건") }
                        AppComboBox {
                            id: ruleConditionType
                            Layout.fillWidth: true
                            enabled: root.selectedRulePath.length > 0 && !root.selectedRuleDetail.otherwise
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: qsTr("확장자"), value: "extension" },
                                { text: qsTr("파일 이름에 포함"), value: "nameContains" },
                                { text: qsTr("파일 크기"), value: "size" },
                                { text: qsTr("수정 후 지난 일수"), value: "modifiedAgeDays" }
                            ]
                            onCurrentValueChanged: ruleConditionComparison.currentIndex = 0
                            onActivated: root.ruleFormDirty = true
                        }
                        AppComboBox {
                            id: ruleConditionComparison
                            Layout.fillWidth: true
                            visible: ruleConditionType.currentValue === "size" ||
                                     ruleConditionType.currentValue === "modifiedAgeDays"
                            textRole: "text"
                            valueRole: "value"
                            model: ruleConditionType.currentValue === "size"
                                ? [
                                    { text: qsTr("초과 (>)"), value: "greaterThan" },
                                    { text: qsTr("이상 (≥)"), value: "atLeast" },
                                    { text: qsTr("미만 (<)"), value: "lessThan" },
                                    { text: qsTr("이하 (≤)"), value: "atMost" }
                                  ]
                                : [
                                    { text: qsTr("이상 (≥)"), value: "atLeast" },
                                    { text: qsTr("미만 (<)"), value: "lessThan" }
                                  ]
                            onActivated: root.ruleFormDirty = true
                        }
                        Label {
                            text: ruleConditionType.currentValue === "extension" ? qsTr("확장자 (쉼표로 구분)")
                                : ruleConditionType.currentValue === "nameContains" ? qsTr("이름에 포함할 글자")
                                : ruleConditionType.currentValue === "size" ? qsTr("크기")
                                : qsTr("수정 후 지난 일수")
                        }
                        AppTextField {
                            id: ruleConditionValue
                            Layout.fillWidth: true
                            enabled: root.selectedRulePath.length > 0 && !root.selectedRuleDetail.otherwise
                            inputMethodHints: ruleConditionType.currentValue === "size" ||
                                              ruleConditionType.currentValue === "modifiedAgeDays"
                                              ? Qt.ImhDigitsOnly : Qt.ImhNone
                            placeholderText: ruleConditionType.currentValue === "extension"
                                             ? qsTr("예: png, jpg") : qsTr("값 입력")
                            onTextEdited: root.ruleFormDirty = true
                        }
                        AppComboBox {
                            id: ruleSizeUnit
                            Layout.fillWidth: true
                            visible: ruleConditionType.currentValue === "size"
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: "MiB (1 MiB = 1,048,576 B)", value: "MiB" },
                                { text: "B", value: "B" },
                                { text: "KB", value: "KB" },
                                { text: "MB", value: "MB" },
                                { text: "GB", value: "GB" }
                            ]
                            onActivated: root.ruleFormDirty = true
                        }
                        Label {
                            text: root.selectedRuleDetail.exclude
                                  ? qsTr("정리 제외 · 이 규칙에 맞으면 이동하지 않음")
                                  : qsTr("목적지 (자식이 있으면 비워 둠)")
                        }
                        AppTextField {
                            id: ruleDestinationField
                            Layout.fillWidth: true
                            enabled: root.selectedRulePath.length > 0 && !root.selectedRuleDetail.exclude
                            placeholderText: qsTr("예: Pictures/PNG")
                            onTextEdited: root.ruleFormDirty = true
                        }
                        AppButton {
                            text: qsTr("이 규칙 수정")
                            enabled: root.selectedRulePath.length > 0
                            onClicked: {
                                if (ruleEditor.edit(root.selectedRulePath, ruleIdField.text,
                                                    ruleEnabledCheck.checked, ruleConditionType.currentValue,
                                                    ruleConditionComparison.currentValue,
                                                    ruleConditionValue.text, ruleSizeUnit.currentValue,
                                                    ruleDestinationField.text))
                                    root.selectRule(root.selectedRulePath)
                            }
                        }
                        RowLayout {
                            AppButton {
                                text: qsTr("자식 추가")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.add(root.selectedRulePath)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule(root.selectedRulePath)
                                    }
                                }
                            }
                            AppButton {
                                text: qsTr("그 아래 '그 외'")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.addOtherwise(root.selectedRulePath)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule(root.selectedRulePath)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            AppButton {
                                text: qsTr("위로")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.move(root.selectedRulePath, -1)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule("")
                                    }
                                }
                            }
                            AppButton {
                                text: qsTr("아래로")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.move(root.selectedRulePath, 1)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule("")
                                    }
                                }
                            }
                            AppButton {
                                text: qsTr("규칙 제거")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    ruleEditor.remove(root.selectedRulePath)
                                    root.collapsedRulePaths = ({})
                                    root.selectRule("")
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    EmbeddedPage {
        id: watchDialog
        objectName: "watchSettingsDialog"
        parent: extensionHost
        pageIndex: 3
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection

        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceCompact

            Label {
                Layout.fillWidth: true
                text: qsTr("조건을 만족하면 정리를 제안합니다. 파일은 자동으로 이동하지 않습니다.")
                wrapMode: Text.WordWrap
            }
            ScrollView {
                id: watchSettingsScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                ColumnLayout {
                    width: watchSettingsScroll.availableWidth
                    spacing: UiMetrics.spaceGroup
                    Label { text: qsTr("감시할 폴더") }
                    AppTextField {
                        id: watchFolderField
                        Layout.fillWidth: true
                        placeholderText: qsTr("예: C:/Users/이름/Downloads")
                    }
                    RowLayout {
                        CheckBox {
                            id: watchEnabledCheck
                            text: qsTr("자동 감시 켜기")
                        }
                        CheckBox {
                            id: watchRecursiveCheck
                            text: qsTr("하위 폴더 포함")
                        }
                    }
                    RowLayout {
                        Label { text: qsTr("정기 재검사 간격") }
                        AppSpinBox {
                            id: watchIntervalSpin
                            objectName: "watchIntervalSpin"
                            editable: true
                            Layout.preferredWidth: UiMetrics.numericInputWidth
                            from: 1
                            to: 1440
                            value: 30
                        }
                        Label { text: qsTr("분") }
                    }
                    RowLayout {
                        Label { text: qsTr("조건 조합") }
                        AppComboBox {
                            id: watchCombination
                            model: [qsTr("하나만 충족"), qsTr("모두 충족")]
                        }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchCountCheck
                            text: qsTr("분류 가능한 파일이")
                        }
                        AppSpinBox {
                            id: watchCountSpin
                            objectName: "watchCountSpin"
                            editable: true
                            Layout.preferredWidth: UiMetrics.numericInputWidth
                            from: 1
                            to: 1000000
                            value: 20
                            enabled: watchCountCheck.checked
                        }
                        Label { text: qsTr("개 이상") }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchSizeCheck
                            text: qsTr("파일 전체 크기가")
                        }
                        AppSpinBox {
                            id: watchSizeSpin
                            objectName: "watchSizeSpin"
                            editable: true
                            Layout.preferredWidth: UiMetrics.numericInputWidth
                            from: 1
                            to: 1000000
                            value: 100
                            enabled: watchSizeCheck.checked
                        }
                        AppComboBox {
                            id: watchSizeUnit
                            objectName: "watchSizeUnitCombo"
                            enabled: watchSizeCheck.checked
                            model: [
                                { text: "KB", value: "KB" },
                                { text: "MB", value: "MB" },
                                { text: "GB", value: "GB" },
                                { text: "MiB", value: "MiB" }
                            ]
                            textRole: "text"
                            valueRole: "value"
                            currentIndex: 1
                        }
                        Label { text: qsTr("이상") }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchAgeCheck
                            text: qsTr("가장 오래된 파일이")
                        }
                        AppSpinBox {
                            id: watchAgeSpin
                            objectName: "watchAgeSpin"
                            editable: true
                            Layout.preferredWidth: UiMetrics.numericInputWidth
                            from: 1
                            to: 1000000
                            value: 60
                            enabled: watchAgeCheck.checked
                        }
                        Label { text: qsTr("분 이상") }
                    }
                    Label { text: qsTr("제외할 파일명 패턴 · 쉼표로 구분") }
                    AppTextField {
                        id: watchExcludeField
                        Layout.fillWidth: true
                        placeholderText: qsTr("*.partial, *.crdownload, ~$*")
                    }
                }
            }
            Label {
                Layout.fillWidth: true
                text: watchController.statusMessage
                wrapMode: Text.WordWrap
                color: root.editorDark ? "#b6d8c5" : "#426349"
            }
            RowLayout {
                AppButton {
                    text: qsTr("조건 저장")
                    onClicked: watchController.saveSettings(
                        watchFolderField.text, watchEnabledCheck.checked,
                        watchRecursiveCheck.checked, watchIntervalSpin.value,
                        watchCombination.currentIndex === 1,
                        watchCountCheck.checked, watchCountSpin.value,
                        watchSizeCheck.checked, watchSizeSpin.value,
                        watchAgeCheck.checked, watchAgeSpin.value,
                        watchExcludeField.text, watchSizeUnit.currentValue)
                }
                AppButton {
                    text: qsTr("저장된 조건 지금 검사")
                    onClicked: watchController.checkNow()
                }
            }
        }
    }

    Dialog {
        enabled: !backgroundTasks.busy
        id: watchProposalDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 700)
        height: Math.min(root.height - 32, 500)
        modal: true
        title: qsTr("정리 제안")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                text: watchController.proposalText
                wrapMode: Text.WordWrap
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: watchController.proposalFiles
                delegate: Label {
                    required property string modelData
                    width: parent.width
                    text: modelData
                    wrapMode: Text.WrapAnywhere
                    padding: UiMetrics.spaceSmall
                }
            }
            RowLayout {
                AppButton {
                    text: qsTr("대기 목록에 담기")
                    enabled: watchController.proposalVisible
                    onClicked: {
                        watchController.addProposalToPending()
                        watchProposalDialog.close()
                        root.currentPage = 0
                    }
                }
                AppButton {
                    text: qsTr("30분 뒤 다시 알림")
                    onClicked: {
                        watchController.snoozeProposal()
                        watchProposalDialog.close()
                    }
                }
            }
        }
    }

    EmbeddedPage {
        id: historyDialog
        objectName: "historyDialog"
        parent: extensionHost
        pageIndex: 4
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection

        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceCompact

            Label {
                Layout.fillWidth: true
                text: historyController.historyMessage
                wrapMode: Text.WordWrap
                color: root.editorDark ? "#b6d8c5" : "#426349"
            }
            AppButton {
                text: qsTr("목록 새로고침")
                onClicked: historyController.refreshHistory()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: UiMetrics.spaceGroup

                ListView {
                    id: historyList
                    objectName: "historyList"
                    Layout.preferredWidth: UiMetrics.historyListWidth
                    Layout.fillHeight: true
                    clip: true
                    spacing: UiMetrics.spaceSmall
                    model: historyController.historyEntries
                    delegate: Column {
                        required property var modelData
                        width: historyList.width
                        spacing: UiMetrics.spaceControl
                        Label {
                            objectName: "historyDateHeading"
                            visible: modelData.startsDateGroup
                            height: visible ? implicitHeight : 0
                            text: modelData.dateGroup
                            color: root.primaryText
                            font.pixelSize: UiMetrics.textSectionTitle
                            font.weight: UiMetrics.groupTitleWeight
                        }
                        Rectangle {
                            width: parent.width
                            height: UiMetrics.historyRowHeight
                            radius: UiMetrics.radiusControl
                            color: modelData.planId === historyController.selectedHistoryPlanId
                                   ? root.theme.menuSelected : root.theme.alternate
                            border.color: root.borderColor

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: UiMetrics.spaceCompact
                                spacing: UiMetrics.spaceTiny
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.time + " · " + modelData.status
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Label {
                                    text: qsTr("대상 %1개 · 현재 이동됨 %2개")
                                          .arg(modelData.fileCount).arg(modelData.appliedCount)
                                    color: root.mutedText
                                }
                                AppButton {
                                    text: qsTr("파일 보기")
                                    onClicked: historyController.selectHistoryPlan(modelData.planId)
                                }
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: UiMetrics.spaceControl

                    Label {
                        Layout.fillWidth: true
                        text: historyController.selectedHistoryPlanId.length > 0
                              ? qsTr("선택한 작업의 파일") : qsTr("왼쪽에서 작업을 선택해 주세요.")
                        font.bold: true
                    }
                    ScrollView {
                        id: historyStepScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        Column {
                            width: historyStepScroll.availableWidth
                            spacing: UiMetrics.spaceSmall

                            Repeater {
                                model: historyController.historySteps
                                delegate: Frame {
                                    required property var modelData
                                    width: parent.width

                                    ColumnLayout {
                                        width: parent.width
                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.fileName + " · " + modelData.status
                                            font.bold: true
                                            wrapMode: Text.WrapAnywhere
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("원본: ") + modelData.source
                                            wrapMode: Text.WrapAnywhere
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            text: qsTr("목적지: ") + modelData.destination
                                            wrapMode: Text.WrapAnywhere
                                        }
                                    }
                                }
                            }
                        }
                    }
                    AppButton {
                        text: qsTr("이 작업 되돌리기")
                        enabled: historyController.canUndoSelectedHistory
                        onClicked: undoHistoryDialog.open()
                    }
                }
            }
        }
    }

    Dialog {
        enabled: !backgroundTasks.busy
        id: undoHistoryDialog
        parent: extensionWindow.contentItem
        anchors.centerIn: parent
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        modal: true
        title: qsTr("선택한 작업 되돌리기")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: historyController.undoSelectedHistory()

        Label {
            width: parent.width
            text: qsTr("이 작업에서 이동된 파일 %1개를 원래 위치로 되돌릴까요?\n"
                       + "파일이 바뀌었거나 원본 경로가 사용 중이면 중단합니다.")
                  .arg(historyController.selectedHistoryFileCount)
            wrapMode: Text.WordWrap
        }
    }

    EmbeddedPage {
        id: recoveryDialog
        objectName: "recoveryDialog"
        parent: extensionHost
        pageIndex: 10
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset

        ColumnLayout {
            anchors.fill: parent
            spacing: UiMetrics.spaceCompact

            Label {
                Layout.fillWidth: true
                text: qsTr("시작 검사와 '나중에'는 파일을 변경하지 않습니다. 재개·되돌리기는 직접 선택하고 확인해야 실행됩니다.")
                wrapMode: Text.WordWrap
            }
            AppButton {
                text: qsTr("파일 상태 다시 검사")
                enabled: !recoveryController.recoveryBusy
                onClicked: recoveryController.refreshRecoveryCases()
            }
            Label {
                Layout.fillWidth: true
                text: recoveryController.recoveryMessage
                wrapMode: Text.WordWrap
                color: root.editorDark ? "#e5bf85" : "#9b5c23"
            }
            ScrollView {
                id: recoveryScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Column {
                    width: recoveryScroll.availableWidth
                    spacing: UiMetrics.spaceControl

                    Repeater {
                        model: recoveryController.recoveryCases
                        delegate: Frame {
                            id: recoveryCaseFrame
                            required property var modelData
                            width: parent.width

                            ColumnLayout {
                                width: parent.width
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("작업 기록: ") + modelData.planId +
                                          (modelData.whole ? "" : qsTr(" · 파일 ") + modelData.partIndex + "/" + modelData.partCount)
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !!modelData.whole
                                    text: qsTr("이동 완료 %1 · 미시작 %2 · 복원 완료 %3\n확인 필요 %4 · 보존 임시 파일 %5")
                                        .arg(modelData.moved || 0).arg(modelData.pending || 0).arg(modelData.restored || 0)
                                        .arg(modelData.needsAttention || 0).arg(modelData.preserved || 0)
                                    wrapMode: Text.WordWrap
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !modelData.whole
                                    text: qsTr("원본 (") + modelData.sourceState + "): " + modelData.source
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    visible: !modelData.whole
                                    text: qsTr("목적지 (") + modelData.destinationState + "): " + modelData.destination
                                    wrapMode: Text.WrapAnywhere
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.detail
                                    wrapMode: Text.WordWrap
                                    color: root.editorDark ? "#e5bf85" : "#9b5c23"
                                }
                                CheckBox {
                                    id: recoveryPathsToggle
                                    visible: !!modelData.whole
                                    text: qsTr("파일 경로 보기")
                                }
                                Repeater {
                                    model: recoveryPathsToggle.checked && recoveryCaseFrame.modelData.whole ? recoveryCaseFrame.modelData.files : []
                                    delegate: Label {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        text: "[" + modelData.state + qsTr("] 원본: ") + modelData.source + qsTr("\n목적지: ") + modelData.destination +
                                            (modelData.staging ? qsTr("\n임시 경로: ") + modelData.staging : "")
                                        textFormat: Text.PlainText
                                        wrapMode: Text.WrapAnywhere
                                    }
                                }
                                RowLayout {
                                    visible: !!modelData.whole
                                    AppButton {
                                        text: qsTr("재개")
                                        enabled: recoveryCaseFrame.modelData.canResume && !recoveryController.recoveryBusy
                                        onClicked: recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "resume")
                                    }
                                    AppButton {
                                        text: qsTr("되돌리기")
                                        enabled: recoveryCaseFrame.modelData.canRollback && !recoveryController.recoveryBusy
                                        onClicked: recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "rollback")
                                    }
                                    AppButton {
                                        text: qsTr("나중에")
                                        enabled: !recoveryController.recoveryBusy
                                        onClicked: { recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "later"); recoveryDialog.close() }
                                    }
                                }
                                AppButton {
                                    text: qsTr("현재 상태로 기록 확정")
                                    visible: modelData.partIndex === 1 && (!modelData.whole || modelData.canResolve)
                                    enabled: modelData.canResolve && !recoveryController.recoveryBusy
                                    onClicked: recoveryController.resolveRecoveryCase(modelData.planId)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    Connections {
        target: recoveryController
        function onRecoveryConfirmationChanged() {
            if (recoveryController.awaitingRecoveryConfirmation) recoveryConfirmDialog.open()
            else recoveryConfirmDialog.close()
        }
    }
    Dialog {
        enabled: !backgroundTasks.busy
        id: recoveryConfirmDialog
        objectName: "recoveryConfirmDialog"
        parent: extensionWindow.contentItem
        anchors.centerIn: parent
        width: Math.min(parent.width - UiMetrics.pageInset * 2, UiMetrics.confirmationContentWidth)
        modal: true
        title: qsTr("중단 작업 실행 확인")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: recoveryController.confirmRecoveryChoice()
        onRejected: recoveryController.cancelRecoveryChoice()
        Label {
            width: parent.width
            text: recoveryController.recoveryConfirmationText
            textFormat: Text.PlainText
            wrapMode: Text.WrapAnywhere
        }
    }
}

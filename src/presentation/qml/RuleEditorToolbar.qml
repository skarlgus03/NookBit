import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 메뉴·도움말·저장·시험 요청만 담당하며 다른 화면 부품을 직접 조작하지 않는 도구 모음.
ColumnLayout {
    id: toolbar
    required property var editor
    required property Item canvasHost
    property string guideStatus: ""
    property bool testOpened: false
    signal saveRequested
    signal testRequested
    signal importRequested
    signal exportRequested
    signal reloadRequested
    signal guideRequested
    signal addRequested
    signal focusCanvasRequested
    spacing: UiMetrics.spaceGroup
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property color surfaceColor: theme.surface
    readonly property color borderColor: theme.border
    readonly property color primaryText: theme.text
    readonly property color mutedText: theme.muted
    readonly property var shortcutRows: [
        {
            keys: qsTr("우클릭"),
            action: qsTr("블록 추가")
        },
        {
            keys: qsTr("우드래그"),
            action: qsTr("화면 이동")
        },
        {
            keys: qsTr("마우스 휠"),
            action: qsTr("확대 · 축소")
        },
        {
            keys: qsTr("좌드래그"),
            action: qsTr("범위 선택")
        },
        {
            keys: qsTr("Shift + 클릭"),
            action: qsTr("추가 선택")
        },
        {
            keys: qsTr("Ctrl + 드래그"),
            action: qsTr("부모 · 형제 기준 스냅")
        },
        {
            keys: qsTr("Alt + 선 클릭"),
            action: qsTr("연결 해제")
        },
        {
            keys: qsTr("Delete"),
            action: qsTr("선택 블록 삭제")
        },
        {
            keys: qsTr("Ctrl + C / V"),
            action: qsTr("복사 · 붙여넣기")
        },
        {
            keys: qsTr("Ctrl + Shift + L"),
            action: qsTr("선택 블록 세로 정렬")
        }
    ]
    component ThemeButton: RuleEditorButton {
        dark: toolbar.dark
    }
    component EditorMenu: RuleEditorMenu {
        dark: toolbar.dark
    }
    component MenuAction: ThemeButton {
        Layout.fillWidth: true
        flat: true
        leftPadding: UiMetrics.controlPadding
        rightPadding: UiMetrics.controlPadding
        labelAlignment: Text.AlignLeft
    }
    component MenuButton: ThemeButton {
        id: menuButton
        property bool menuOpen: false
        Layout.minimumWidth: implicitWidth
        rightPadding: UiMetrics.dropdownTrailingPadding
        background: ButtonFeedback {
            button: menuButton
            normalColor: "transparent"
            selected: menuButton.menuOpen
            selectedColor: toolbar.theme.buttonChecked
            idleBorder: menuButton.menuOpen ? toolbar.theme.editorAccent : "transparent"
        }
        DropdownIndicator {
            anchors.right: parent.right
            anchors.rightMargin: UiMetrics.controlPadding
            anchors.verticalCenter: parent.verticalCenter
            strokeColor: toolbar.primaryText
        }
    }
    // 메뉴를 하나만 열고 같은 버튼을 다시 누르면 닫는 함수.
    function toggleMenu(popup) {
        const shouldOpen = !popup.visible;
        for (const candidate of [jsonFileMenu, editMenu, viewMenu, ruleHelpPanel])
            candidate.close();
        if (shouldOpen)
            popup.open();
    }
    // 페이지를 떠날 때 이 도구 모음의 메뉴와 도움말만 닫는 함수.
    function dismissTransientUi() {
        for (const popup of [jsonFileMenu, editMenu, viewMenu, ruleHelpPanel])
            popup.close();
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: UiMetrics.spaceSection
        Rectangle {
            implicitWidth: UiMetrics.controlHeight
            implicitHeight: UiMetrics.controlHeight
            radius: UiMetrics.radiusControl
            color: toolbar.theme.editorTint
            NavigationIcon {
                anchors.centerIn: parent
                width: UiMetrics.navigationIconSize
                height: UiMetrics.navigationIconSize
                kind: "rules"
                strokeColor: toolbar.theme.editorAccent
            }
        }
        Label {
            text: qsTr("분류 규칙")
            font.pixelSize: UiMetrics.textPageTitle
            font.bold: true
            color: toolbar.primaryText
        }
        Item {
            Layout.fillWidth: true
        }
        ThemeButton {
            id: testAction
            Layout.minimumWidth: implicitWidth
            text: qsTr("규칙 테스트")
            objectName: "ruleTestButton"
            background: ButtonFeedback {
                button: testAction
                normalColor: toolbar.surfaceColor
                idleBorder: toolbar.theme.editorAccent
                selected: toolbar.testOpened
            }
            onClicked: toolbar.testRequested()
        }
        ThemeButton {
            objectName: "ruleSaveButton"
            Layout.minimumWidth: implicitWidth
            text: qsTr("전체 저장")
            font.bold: true
            primary: true
            onClicked: {
                toolbar.saveRequested();
            }
        }
    }

    Label {
        Layout.fillWidth: true
        visible: toolbar.guideStatus !== ""
        text: toolbar.guideStatus
        color: toolbar.mutedText
        wrapMode: Text.Wrap
    }

    Rectangle {
        objectName: "ruleEditorToolbar"
        Layout.fillWidth: true
        implicitHeight: toolbarContents.implicitHeight + UiMetrics.spaceControl * 2
        radius: UiMetrics.radiusControl
        color: toolbar.theme.editorTint
        RowLayout {
            id: toolbarContents
            anchors.fill: parent
            anchors.margins: UiMetrics.spaceControl
            spacing: UiMetrics.spaceSection
            MenuButton {
                id: jsonFileButton
                objectName: "ruleFileMenuButton"
                text: qsTr("파일")
                menuOpen: jsonFileMenu.visible
                onClicked: toolbar.toggleMenu(jsonFileMenu)
            }
            MenuButton {
                id: editMenuButton
                objectName: "ruleEditMenuButton"
                text: qsTr("편집")
                menuOpen: editMenu.visible
                onClicked: toolbar.toggleMenu(editMenu)
            }
            MenuButton {
                id: viewMenuButton
                objectName: "ruleViewMenuButton"
                text: qsTr("보기")
                menuOpen: viewMenu.visible
                onClicked: toolbar.toggleMenu(viewMenu)
            }
            ThemeButton {
                objectName: "ruleHelpButton"
                Layout.minimumWidth: implicitWidth
                text: qsTr("도움말")
                flat: true
                hoverEnabled: true
                ToolTip.visible: hovered && !ruleHelpPanel.visible
                ToolTip.delay: UiMetrics.tooltipDelayMs
                ToolTip.text: qsTr("단축키 · 사용법 · AI 작성 문서")
                onClicked: toolbar.toggleMenu(ruleHelpPanel)
            }
            Item {
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("%1%").arg(Math.round(toolbar.editor.zoom * 100))
                color: toolbar.mutedText
                height: UiMetrics.controlHeight
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    EditorMenu {
        id: jsonFileMenu
        objectName: "ruleFileMenu"
        anchorButton: jsonFileButton
        MenuAction {
            text: qsTr("JSON 불러오기")
            onClicked: {
                jsonFileMenu.close();
                toolbar.importRequested();
            }
        }
        MenuAction {
            text: qsTr("JSON 내보내기")
            onClicked: {
                jsonFileMenu.close();
                toolbar.exportRequested();
            }
        }
        MenuAction {
            text: qsTr("저장본 다시 열기")
            onClicked: {
                jsonFileMenu.close();
                toolbar.reloadRequested();
            }
        }
    }

    EditorMenu {
        id: editMenu
        objectName: "ruleEditMenu"
        anchorButton: editMenuButton
        MenuAction {
            text: qsTr("블록 추가")
            onClicked: {
                editMenu.close();
                toolbar.addRequested();
            }
        }
        MenuAction {
            text: qsTr("복사")
            enabled: toolbar.editor.selectedIds.some(id => id !== 1)
            onClicked: {
                editMenu.close();
                toolbar.editor.copySelectedBlocks();
                toolbar.focusCanvasRequested();
            }
        }
        MenuAction {
            text: qsTr("붙여넣기")
            enabled: toolbar.editor.copiedBlocks.length > 0
            onClicked: {
                editMenu.close();
                toolbar.editor.pasteCopiedBlocks();
                toolbar.focusCanvasRequested();
            }
        }
        MenuAction {
            text: qsTr("선택 블록 세로 정렬")
            enabled: toolbar.editor.selectedIds.filter(id => id !== 1).length >= 2
            onClicked: {
                editMenu.close();
                toolbar.editor.alignSelectedVertically();
                toolbar.focusCanvasRequested();
            }
        }
        MenuAction {
            text: qsTr("선택 블록 삭제")
            enabled: toolbar.editor.selectedIds.some(id => id !== 1)
            onClicked: {
                editMenu.close();
                toolbar.editor.removeSelectedBlock();
                toolbar.focusCanvasRequested();
            }
        }
    }

    EditorMenu {
        id: viewMenu
        objectName: "ruleViewMenu"
        anchorButton: viewMenuButton
        Label {
            text: qsTr("연결선 모양")
            color: toolbar.primaryText
            font.bold: true
        }
        AppComboBox {
            objectName: "ruleConnectionStyleCombo"
            Layout.fillWidth: true
            model: [qsTr("곡선"), qsTr("직선")]
            currentIndex: toolbar.editor.connectionStyle
            onActivated: index => {
                toolbar.editor.connectionStyle = index;
                toolbar.editor.repaintRequested();
            }
        }
        Label {
            text: qsTr("글씨 크기")
            color: toolbar.primaryText
            font.bold: true
        }
        AppComboBox {
            objectName: "ruleFontPresetCombo"
            Layout.fillWidth: true
            model: [qsTr("작게"), qsTr("보통"), qsTr("크게")]
            currentIndex: toolbar.editor.fontPresetIndex
            onActivated: index => toolbar.editor.fontPresetIndex = index
        }
        MenuAction {
            text: qsTr("노드 전체 보기")
            onClicked: {
                viewMenu.close();
                toolbar.editor.fitAllNodes();
                toolbar.focusCanvasRequested();
            }
        }
        MenuAction {
            text: qsTr("기본 보기 (100%)")
            onClicked: {
                viewMenu.close();
                toolbar.editor.resetDefaultView();
                toolbar.focusCanvasRequested();
            }
        }
    }

    Popup {
        id: ruleHelpPanel
        objectName: "ruleHelpPanel"
        parent: toolbar.canvasHost
        x: Math.max(0, toolbar.canvasHost.width - width - UiMetrics.spaceSection)
        y: UiMetrics.spaceSection
        width: Math.max(1, Math.min(UiMetrics.ruleHelpWidth, toolbar.canvasHost.width - UiMetrics.spaceSection * 2))
        height: Math.min(helpContents.implicitHeight + topPadding + bottomPadding, Math.max(1, toolbar.canvasHost.height
                                                                                            - UiMetrics.spaceSection
                                                                                            * 2))
        padding: UiMetrics.spaceSection
        modal: false
        dim: false
        focus: true
        closePolicy: Popup.CloseOnEscape
        background: Rectangle {
            color: toolbar.surfaceColor
            radius: UiMetrics.radiusPanel
            border.color: toolbar.borderColor
        }
        contentItem: ScrollView {
            id: helpScroll
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                id: helpContents
                width: helpScroll.availableWidth
                spacing: UiMetrics.spaceSection
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("단축키 · 사용법")
                        font.pixelSize: UiMetrics.textSectionTitle
                        font.bold: true
                        color: toolbar.primaryText
                    }
                    ThemeButton {
                        objectName: "ruleHelpCloseButton"
                        text: "×"
                        implicitWidth: UiMetrics.iconButtonSize
                        Accessible.name: qsTr("도움말 닫기")
                        onClicked: ruleHelpPanel.close()
                    }
                }
                ThemeButton {
                    objectName: "rulesUserGuideButton"
                    Layout.fillWidth: true
                    text: qsTr("AI용 작성 문서 저장")
                    onClicked: {
                        ruleHelpPanel.close();
                        toolbar.guideRequested();
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("블록의 오른쪽 점에서 다음 블록의 왼쪽 점으로 연결하세요. 연결 중 빈 곳에서 놓으면 취소돼요.")
                    wrapMode: Text.Wrap
                    color: toolbar.mutedText
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: UiMetrics.spaceControl
                    Repeater {
                        objectName: "ruleShortcutList"
                        model: toolbar.shortcutRows
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: UiMetrics.spaceGroup
                            Label {
                                Layout.preferredWidth: UiMetrics.shortcutKeyWidth
                                text: modelData.keys
                                font.bold: true
                                font.pixelSize: UiMetrics.textCaption
                                wrapMode: Text.Wrap
                                color: toolbar.primaryText
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.action
                                wrapMode: Text.Wrap
                                color: toolbar.primaryText
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("단축키는 캔버스에 초점이 있을 때 동작해요. 규칙은 저장 전에 검증하며, 노드 위치를 옮겨도 검사 순서는 바뀌지 않아요.")
                    wrapMode: Text.Wrap
                    color: toolbar.mutedText
                }
            }
        }
    }
}

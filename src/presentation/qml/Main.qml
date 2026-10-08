import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T

ApplicationWindow {
    id: root
    // 파일 작업 중에는 입력과 창 닫기를 막고 화면 렌더링은 계속하는 함수.
    onClosing: function(close) {
        if (backgroundTasks.busy) close.accepted = false
    }
    contentItem.enabled: !backgroundTasks.busy
    Popup {
        parent: Overlay.overlay
        visible: backgroundTasks.busy
        modal: true
        dim: false
        focus: true
        closePolicy: Popup.NoAutoClose
        x: (parent.width - width) / 2
        y: parent.height - height - 24
        width: Math.min(440, root.width - 32)
        height: 174
        padding: 10
        background: Rectangle {
            radius: 12
            color: root.editorDark ? "#243044" : "#edf4ef"
            border.color: root.editorDark ? "#617a9c" : "#739b83"
        }
        contentItem: ColumnLayout {
            spacing: 6
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
                color: root.editorDark ? "#b7c6dc" : "#4b6153"
                font.pixelSize: 11
                Layout.fillWidth: true
            }
        }
    }
    property int currentPage: 0
    property string selectedRulePath: ""
    property var selectedRuleDetail: ({})
    property var collapsedRulePaths: ({})
    property bool ruleFormDirty: false
    property bool globalFormDirty: false
    readonly property bool editorDark: currentPage === 1 && ruleEditorPage.darkMode

    // 저장된 감시 설정으로 입력창을 채운 뒤 별도 창을 여는 함수.
    function openWatchSettings() {
        watchFolderField.text = watchController.folder
        watchEnabledCheck.checked = watchController.enabled
        watchRecursiveCheck.checked = watchController.recursive
        watchIntervalSpin.value = watchController.rescanIntervalMinutes
        watchCombination.currentIndex = watchController.combineAll ? 1 : 0
        watchCountCheck.checked = watchController.countEnabled
        watchCountSpin.value = watchController.minimumFileCount
        watchSizeCheck.checked = watchController.sizeEnabled
        watchSizeSpin.value = watchController.minimumTotalMiB
        watchAgeCheck.checked = watchController.ageEnabled
        watchAgeSpin.value = watchController.minimumAgeMinutes
        watchExcludeField.text = watchController.excludePatterns
        watchDialog.open()
    }

    // 최신 작업 기록을 조회하고 별도 창을 여는 함수.
    function openHistory() {
        historyController.refreshHistory()
        historyDialog.open()
    }

    // 탐색 버튼은 선택 상태만 배경에 표시하고 마우스 강조는 테두리로 나타내는 버튼.
    component NavigationButton: T.Button {
        id: navButton
        padding: 8
        implicitHeight: Math.max(36, contentItem.implicitHeight + topPadding + bottomPadding)
        contentItem: Text {
            text: navButton.text
            font: navButton.font
            color: root.editorDark ? "#edf2f8" : "#253428"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 7
            color: root.editorDark
                   ? (navButton.highlighted ? "#3e5270" : "#2b3546")
                   : (navButton.highlighted ? "#dce9db" : "#f3f5f1")
            border.color: navButton.hovered || navButton.visualFocus || navButton.down
                          ? (root.editorDark ? "#86aad6" : "#7da58a")
                          : (root.editorDark ? "#3d4757" : "#d9dfd4")
            border.width: navButton.hovered || navButton.visualFocus || navButton.down ? 2 : 1
        }
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
    width: 1240
    height: 800
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: qsTr("NookBit")
    color: root.editorDark ? "#161a22" : "#f5f5f2"

    Rectangle {
        id: navigation
        width: 190
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        color: root.editorDark ? "#1b2230" : "#edf0e9"
        border.color: root.editorDark ? "#3d4757" : "#d9dfd4"

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 18
            spacing: 10

            Label {
                text: qsTr("NookBit")
                font.pixelSize: 24
                font.bold: true
                color: root.editorDark ? "#edf2f8" : "#253428"
                bottomPadding: 20
            }
            CharacterBadge {
                objectName: "characterBadge"
                width: parent.width
                stateKey: characterController.stateKey
                message: characterController.message
                dark: root.editorDark
            }
            NavigationButton {
                width: parent.width
                text: qsTr("파일 정리")
                highlighted: root.currentPage === 0
                onClicked: root.currentPage = 0
            }
            NavigationButton {
                width: parent.width
                text: qsTr("분류 규칙")
                highlighted: root.currentPage === 1
                onClicked: root.currentPage = 1
            }
            NavigationButton {
                width: parent.width
                text: qsTr("폴더 감시")
                highlighted: watchDialog.visible
                onClicked: root.openWatchSettings()
            }
            NavigationButton {
                width: parent.width
                text: qsTr("작업 기록")
                highlighted: historyDialog.visible
                onClicked: root.openHistory()
            }
            Label {
                width: parent.width
                topPadding: 14
                text: watchController.enabled ? qsTr("폴더 감시 중") : qsTr("폴더 감시 꺼짐")
                wrapMode: Text.WordWrap
                color: root.editorDark ? "#aab6c7" : "#68756b"
                font.pixelSize: 12
            }
            NavigationButton {
                width: parent.width
                visible: watchController.proposalVisible
                highlighted: true
                text: qsTr("새 정리 제안 확인")
                onClicked: watchProposalDialog.open()
            }
        }
    }

    ScrollView {
        id: sortingScroll
        anchors.fill: parent
        anchors.leftMargin: navigation.width + 28
        anchors.rightMargin: 28
        anchors.topMargin: 28
        anchors.bottomMargin: 28
        visible: root.currentPage === 0
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: sortingScroll.availableWidth
            spacing: 14

            Label {
                text: qsTr("파일 정리")
                font.pixelSize: 30
                font.bold: true
                color: "#232323"
            }

            Label {
                text: qsTr("정리할 파일을 먼저 대기 목록에 담아 주세요.")
                font.pixelSize: 16
                color: "#5a5a56"
            }

            RowLayout {
                Layout.fillWidth: true
                Label {
                    Layout.fillWidth: true
                    text: qsTr("현재 적용 중인 규칙: ") + previewController.activeRuleLabel
                    color: "#426349"
                    elide: Text.ElideMiddle
                }
                Button {
                    text: qsTr("규칙 편집기로")
                    onClicked: root.currentPage = 1
                }
            }

            Label {
                Layout.fillWidth: true
                visible: recoveryController.recoveryNotice.length > 0
                text: recoveryController.recoveryNotice
                color: "#9b5c23"
                wrapMode: Text.WordWrap
            }

            Button {
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
                color: dragging ? "#e8f4e5" : "#ffffff"
                border.width: dragging ? 2 : 1
                border.color: dragging ? "#4d8f57" : "#d8dcd5"

                Column {
                    anchors.centerIn: parent
                    spacing: 8

                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("파일을 여기로 끌어놓으세요")
                        font.pixelSize: 19
                        font.bold: true
                        color: "#24382a"
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("원본 파일은 이동하거나 복사하지 않습니다.")
                        color: "#657065"
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
                    font.pixelSize: 19
                    font.bold: true
                    color: "#232323"
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%1개 대기 중").arg(pendingFiles.count)
                    color: "#5a5a56"
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 16
                color: "#ffffff"
                border.color: "#deded8"

                Label {
                    anchors.centerIn: parent
                    visible: pendingFiles.count === 0
                    text: qsTr("아직 대기 중인 파일이 없어요.")
                    color: "#7b817a"
                }

                ListView {
                    id: pendingList
                    anchors.fill: parent
                    anchors.margins: 12
                    clip: true
                    spacing: 4
                    visible: pendingFiles.count > 0
                    model: pendingFiles

                    delegate: Rectangle {
                        required property int index
                        required property string fileName
                        required property string fullPath
                        required property bool available
                        width: pendingList.width
                        height: 70
                        radius: 10
                        color: !available ? "#fff0ec" : index % 2 === 0 ? "#f7f9f6" : "#ffffff"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 12
                            spacing: 12

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3
                                Label {
                                    text: available ? fileName : qsTr("[파일 없음] %1").arg(fileName)
                                    font.bold: true
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: fullPath
                                    color: "#67716a"
                                    elide: Text.ElideMiddle
                                    Layout.fillWidth: true
                                }
                            }

                            Button {
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
                color: "#b44732"
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                visible: pendingFiles.storageNotice.length > 0
                text: pendingFiles.storageNotice
                color: "#b44732"
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Label { text: qsTr("같은 이름의 파일이 있으면"); color: "#232323" }
                ComboBox {
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
                    color: "#67716a"
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Label {
                    text: qsTr("정리할 위치")
                    color: "#232323"
                }
                TextField {
                    id: destinationField
                    Layout.fillWidth: true
                    text: previewController.defaultDestinationRoot
                    placeholderText: qsTr("정리할 폴더의 전체 경로")
                    onTextEdited: previewController.clear()
                }
                Button {
                    text: qsTr("미리보기 만들기")
                    enabled: pendingFiles.count > 0
                    onClicked: previewController.createPreview(destinationField.text)
                }
                Button {
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
                spacing: 12

                Button {
                    text: qsTr("마지막 이동 되돌리기")
                    enabled: previewController.canUndo
                    onClicked: previewController.undoLastMove()
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("중간에 실패하면 되돌리기를 시도하고, 불확실하면 확인을 요청해요.")
                    color: "#67716a"
                }
            }

            Label {
                Layout.fillWidth: true
                text: previewController.message.length > 0
                      ? previewController.message
                      : qsTr("저장된 규칙으로 미리보기를 만듭니다. 편집 중인 초안은 전체 저장 후 적용돼요.")
                color: "#426349"
                wrapMode: Text.WordWrap
            }

            Label {
                text: qsTr("2. 분류 미리보기")
                font.pixelSize: 19
                font.bold: true
                color: "#232323"
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 300
                radius: 16
                color: "#ffffff"
                border.color: "#deded8"

                Label {
                    anchors.centerIn: parent
                    visible: !previewController.hasPreview
                    text: qsTr("미리보기를 만들면 예상 목적지가 여기에 표시됩니다.")
                    color: "#7b817a"
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: previewController.hasPreview
                    spacing: 6

                    ListView {
                        id: previewList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 8
                        model: previewController.outcomes
                        delegate: Rectangle {
                            required property var modelData
                            width: previewList.width
                            height: previewRow.implicitHeight + 20
                            radius: 10
                            color: modelData.statusKey === "planned" ? "#f3f9f2" : "#f7f7f4"
                            border.color: modelData.statusKey === "planned" ? "#b9d8b9" : "#ddddda"
                            ColumnLayout {
                                id: previewRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 10
                                spacing: 3
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
                                        color: modelData.statusKey === "planned" ? "#327542" : "#805b2c"
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.reason
                                    color: "#344039"
                                    elide: Text.ElideRight
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("원본: ") + modelData.source
                                    color: "#67716a"
                                    elide: Text.ElideMiddle
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.destination.length > 0
                                          ? qsTr("예상 목적지: ") + modelData.destination
                                          : qsTr("이동하지 않음 · 원본 유지")
                                    color: "#67716a"
                                    elide: Text.ElideMiddle
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: previewController.outcomes.length === 0
                        text: qsTr("표시할 파일별 검사 결과가 없어요. 위의 경고를 확인해 주세요.")
                        color: "#7b817a"
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: previewController.warnings.length > 0
                        text: qsTr("추가 경고: ") + previewController.warnings.join("\n")
                        color: "#9b5c23"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: pendingFiles.lastMessage.length > 0
                      ? pendingFiles.lastMessage
                      : qsTr("승인 전에는 파일을 이동하지 않습니다.")
                color: "#426349"
                wrapMode: Text.WordWrap
            }
        }
    }

    RuleEditorPage {
        id: ruleEditorPage
        objectName: "ruleEditorPage"
        oldEditorFormDirty: root.ruleFormDirty || root.globalFormDirty
        anchors.fill: parent
        anchors.leftMargin: navigation.width
        visible: root.currentPage === 1
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
            if (previewController.awaitingConfirmation && !sortConfirmation.visible)
                sortConfirmation.open()
            else if (!previewController.awaitingConfirmation && sortConfirmation.visible)
                sortConfirmation.close()
        }
    }

    Dialog {
        id: sortConfirmation
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 760)
        height: Math.min(root.height - 32, 520)
        modal: true
        title: qsTr("파일 이동 최종 확인")
        onClosed: previewController.cancelSort()

        ColumnLayout {
            anchors.fill: parent
            spacing: 12
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
                spacing: 10
                model: previewController.confirmationSteps
                ScrollBar.vertical: ScrollBar {}
                delegate: Column {
                    required property var modelData
                    width: ListView.view.width
                    spacing: 4
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
                Button { text: qsTr("취소"); onClicked: sortConfirmation.close() }
                Button {
                    text: qsTr("확인하고 이동")
                    enabled: previewController.awaitingConfirmation
                    onClicked: previewController.confirmSort()
                }
            }
        }
    }

    Dialog {
        id: ruleDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 820)
        height: Math.min(root.height - 32, 660)
        modal: true
        title: qsTr("분류 규칙 편집")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: qsTr("위에서부터 검사하고, 한 가지가 맞으면 그 아래로 내려갑니다. 목적지는 정리 기준 폴더 안의 상대 경로입니다.")
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: ruleEditor.message
                color: ruleEditor.dirty ? "#9b5c23" : "#426349"
                wrapMode: Text.WordWrap
            }
            Button {
                visible: ruleEditor.legacyRules
                text: qsTr("기존 규칙 백업 후 새 형식으로 변환")
                onClicked: ruleEditor.migrateLegacyRules()
            }
            RowLayout {
                Layout.fillWidth: true
                Button {
                    text: qsTr("루트 규칙 추가")
                    onClicked: {
                        if (root.commitRuleForm() && ruleEditor.add("")) {
                            root.collapsedRulePaths = ({})
                            root.selectRule("")
                        }
                    }
                }
                Button {
                    text: qsTr("루트 '그 외' 추가")
                    onClicked: {
                        if (root.commitRuleForm() && ruleEditor.addOtherwise("")) {
                            root.collapsedRulePaths = ({})
                            root.selectRule("")
                        }
                    }
                }
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("저장 전 변경 버리기")
                    enabled: ruleEditor.dirty || root.ruleFormDirty || root.globalFormDirty
                    onClicked: {
                        ruleEditor.reload()
                        root.selectRule("")
                        globalDestinationField.text = ruleEditor.globalDestination
                        root.globalFormDirty = false
                    }
                }
                Button {
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
                TextField {
                    id: globalDestinationField
                    Layout.fillWidth: true
                    text: ruleEditor.globalDestination
                    placeholderText: qsTr("비우면 미분류")
                    onTextEdited: root.globalFormDirty = true
                }
                Button {
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
                        spacing: 4
                        Repeater {
                            model: ruleEditor.nodes
                            delegate: Row {
                                required property var modelData
                                width: parent.width
                                visible: root.ruleVisible(modelData.path)
                                height: visible ? 36 : 0
                                spacing: 2
                                Item { width: modelData.depth * 16; height: 1 }
                                Button {
                                    width: 32
                                    height: 34
                                    flat: true
                                    visible: modelData.childrenCount > 0
                                    text: root.collapsedRulePaths[modelData.path] ? "▸" : "▾"
                                    onClicked: root.toggleRule(modelData.path)
                                }
                                Button {
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
                        spacing: 8
                        Label {
                            text: root.selectedRulePath.length > 0 ? qsTr("선택한 규칙") : qsTr("왼쪽에서 규칙을 선택하세요")
                            font.bold: true
                        }
                        Label { text: qsTr("규칙 ID (영문·숫자)") }
                        TextField {
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
                        ComboBox {
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
                        ComboBox {
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
                        TextField {
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
                        ComboBox {
                            id: ruleSizeUnit
                            Layout.fillWidth: true
                            visible: ruleConditionType.currentValue === "size"
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { text: "MiB (1 MiB = 1,048,576 B)", value: "MiB" },
                                { text: "B", value: "B" }
                            ]
                            onActivated: root.ruleFormDirty = true
                        }
                        Label {
                            text: root.selectedRuleDetail.exclude
                                  ? qsTr("정리 제외 · 이 규칙에 맞으면 이동하지 않음")
                                  : qsTr("목적지 (자식이 있으면 비워 둠)")
                        }
                        TextField {
                            id: ruleDestinationField
                            Layout.fillWidth: true
                            enabled: root.selectedRulePath.length > 0 && !root.selectedRuleDetail.exclude
                            placeholderText: qsTr("예: Pictures/PNG")
                            onTextEdited: root.ruleFormDirty = true
                        }
                        Button {
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
                            Button {
                                text: qsTr("자식 추가")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.add(root.selectedRulePath)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule(root.selectedRulePath)
                                    }
                                }
                            }
                            Button {
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
                            Button {
                                text: qsTr("위로")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.move(root.selectedRulePath, -1)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule("")
                                    }
                                }
                            }
                            Button {
                                text: qsTr("아래로")
                                enabled: root.selectedRulePath.length > 0
                                onClicked: {
                                    if (root.commitRuleForm() && ruleEditor.move(root.selectedRulePath, 1)) {
                                        root.collapsedRulePaths = ({})
                                        root.selectRule("")
                                    }
                                }
                            }
                            Button {
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

    Dialog {
        id: watchDialog
        objectName: "watchSettingsDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 720)
        height: Math.min(root.height - 32, 640)
        modal: true
        title: qsTr("폴더 감시 조건")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

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
                    spacing: 12
                    Label { text: qsTr("감시할 폴더") }
                    TextField {
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
                        SpinBox {
                            id: watchIntervalSpin
                            from: 1
                            to: 1440
                            value: 30
                        }
                        Label { text: qsTr("분") }
                    }
                    RowLayout {
                        Label { text: qsTr("조건 조합") }
                        ComboBox {
                            id: watchCombination
                            model: [qsTr("하나만 충족"), qsTr("모두 충족")]
                        }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchCountCheck
                            text: qsTr("분류 가능한 파일이")
                        }
                        SpinBox {
                            id: watchCountSpin
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
                        SpinBox {
                            id: watchSizeSpin
                            from: 1
                            to: 1000000
                            value: 100
                            enabled: watchSizeCheck.checked
                        }
                        Label { text: qsTr("MiB 이상") }
                    }
                    RowLayout {
                        CheckBox {
                            id: watchAgeCheck
                            text: qsTr("가장 오래된 파일이")
                        }
                        SpinBox {
                            id: watchAgeSpin
                            from: 1
                            to: 1000000
                            value: 60
                            enabled: watchAgeCheck.checked
                        }
                        Label { text: qsTr("분 이상") }
                    }
                    Label { text: qsTr("제외할 파일명 패턴 · 쉼표로 구분") }
                    TextField {
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
                color: "#426349"
            }
            RowLayout {
                Button {
                    text: qsTr("조건 저장")
                    onClicked: watchController.saveSettings(
                        watchFolderField.text, watchEnabledCheck.checked,
                        watchRecursiveCheck.checked, watchIntervalSpin.value,
                        watchCombination.currentIndex === 1,
                        watchCountCheck.checked, watchCountSpin.value,
                        watchSizeCheck.checked, watchSizeSpin.value,
                        watchAgeCheck.checked, watchAgeSpin.value,
                        watchExcludeField.text)
                }
                Button {
                    text: qsTr("저장된 조건 지금 검사")
                    onClicked: watchController.checkNow()
                }
            }
        }
    }

    Dialog {
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
                    padding: 5
                }
            }
            RowLayout {
                Button {
                    text: qsTr("대기 목록에 담기")
                    enabled: watchController.proposalVisible
                    onClicked: {
                        watchController.addProposalToPending()
                        watchProposalDialog.close()
                        root.currentPage = 0
                    }
                }
                Button {
                    text: qsTr("30분 뒤 다시 알림")
                    onClicked: {
                        watchController.snoozeProposal()
                        watchProposalDialog.close()
                    }
                }
            }
        }
    }

    Dialog {
        id: historyDialog
        objectName: "historyDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 840)
        height: Math.min(root.height - 32, 600)
        modal: true
        title: qsTr("지난 작업 기록")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: historyController.historyMessage
                wrapMode: Text.WordWrap
                color: "#426349"
            }
            Button {
                text: qsTr("목록 새로고침")
                onClicked: historyController.refreshHistory()
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 12

                ListView {
                    id: historyList
                    Layout.preferredWidth: 280
                    Layout.fillHeight: true
                    clip: true
                    spacing: 6
                    model: historyController.historyEntries
                    delegate: Rectangle {
                        required property var modelData
                        width: historyList.width
                        height: 100
                        radius: 9
                        color: modelData.planId === historyController.selectedHistoryPlanId
                               ? "#e8f4e5" : "#f7f9f6"
                        border.color: "#d8dcd5"

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 9
                            spacing: 2
                            Label {
                                Layout.fillWidth: true
                                text: modelData.time + " · " + modelData.status
                                font.bold: true
                                elide: Text.ElideRight
                            }
                            Label {
                                text: qsTr("대상 %1개 · 현재 이동됨 %2개")
                                      .arg(modelData.fileCount).arg(modelData.appliedCount)
                                color: "#67716a"
                            }
                            Button {
                                text: qsTr("파일 보기")
                                onClicked: historyController.selectHistoryPlan(modelData.planId)
                            }
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

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
                            spacing: 6

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
                    Button {
                        text: qsTr("이 작업 되돌리기")
                        enabled: historyController.canUndoSelectedHistory
                        onClicked: undoHistoryDialog.open()
                    }
                }
            }
        }
    }

    Dialog {
        id: undoHistoryDialog
        anchors.centerIn: parent
        width: Math.min(root.width - 80, 480)
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

    Dialog {
        id: recoveryDialog
        objectName: "recoveryDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 760)
        height: Math.min(root.height - 32, 560)
        modal: true
        title: qsTr("중단 작업 확인")
        standardButtons: Dialog.Close

        ColumnLayout {
            anchors.fill: parent
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("시작 검사와 '나중에'는 파일을 변경하지 않습니다. 재개·되돌리기는 직접 선택하고 확인해야 실행됩니다.")
                wrapMode: Text.WordWrap
            }
            Button {
                text: qsTr("파일 상태 다시 검사")
                enabled: !recoveryController.recoveryBusy
                onClicked: recoveryController.refreshRecoveryCases()
            }
            Label {
                Layout.fillWidth: true
                text: recoveryController.recoveryMessage
                wrapMode: Text.WordWrap
                color: "#9b5c23"
            }
            ScrollView {
                id: recoveryScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Column {
                    width: recoveryScroll.availableWidth
                    spacing: 8

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
                                    color: "#9b5c23"
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
                                    Button {
                                        text: qsTr("재개")
                                        enabled: recoveryCaseFrame.modelData.canResume && !recoveryController.recoveryBusy
                                        onClicked: recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "resume")
                                    }
                                    Button {
                                        text: qsTr("되돌리기")
                                        enabled: recoveryCaseFrame.modelData.canRollback && !recoveryController.recoveryBusy
                                        onClicked: recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "rollback")
                                    }
                                    Button {
                                        text: qsTr("나중에")
                                        enabled: !recoveryController.recoveryBusy
                                        onClicked: { recoveryController.requestRecoveryChoice(recoveryCaseFrame.modelData.planId, "later"); recoveryDialog.close() }
                                    }
                                }
                                Button {
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
        id: recoveryConfirmDialog
        objectName: "recoveryConfirmDialog"
        anchors.centerIn: parent
        width: Math.min(root.width - 32, 560)
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

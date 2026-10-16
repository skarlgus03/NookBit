import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Templates as T
import QtQuick.Dialogs

// 파일 대기 목록·정리 위치·정리 요청은 홈과 분리된 페이지에서만 표시한다.
Rectangle {
    id: home
    objectName: "fileSortPage"
    property bool dark: false
    property int workflowStep: 0
    property int departingStep: 0
    property real transitionProgress: 1
    property bool manualSortRequested: false
    property int confirmationReturnStep: 0
    property bool manualSession: false
    property bool sortSuccess: false
    property string resultMessage: ""
    property int previewAction: 0
    readonly property bool previewOpen: workflowStep === 1
    readonly property bool transitioning: slideAnimation.running
    clip: true
    NumberAnimation {
        id: slideAnimation
        target: home
        property: "transitionProgress"
        from: 0; to: 1
        duration: UiMetrics.pageSlideDuration
        easing.type: Easing.OutCubic
    }
    // 출발·도착 화면만 옆으로 움직여 중간 화면을 지나치지 않게 하는 함수.
    function showStep(step) {
        if (workflowStep === step) return
        slideAnimation.stop()
        departingStep = workflowStep
        workflowStep = step
        transitionProgress = 0
        if (visible) slideAnimation.restart()
        else transitionProgress = 1
    }
    // 현재 전환 방향에 맞는 화면의 가로 위치를 반환하는 함수.
    function paneX(step) {
        const direction = workflowStep > departingStep ? 1 : -1
        if (step === workflowStep) return width * direction * (1 - transitionProgress)
        if (step === departingStep) return -width * direction * transitionProgress
        return width
    }
    // 현재 화면과 전환 중인 직전 화면만 표시하는 함수.
    function paneVisible(step) { return workflowStep === step || (transitioning && departingStep === step) }
    // 화면에서 요청한 정리만 표시 대상으로 기억하고 기존 확인 준비를 호출하는 함수.
    function requestSort() {
        confirmationReturnStep = workflowStep
        manualSortRequested = true
        previewController.prepareSort()
        manualSortRequested = backgroundTasks.busy || previewController.awaitingConfirmation
        if (previewController.awaitingConfirmation) showStep(2)
    }
    // 최종 확인을 취소하고 준비 전 입력 또는 미리보기로 돌아가는 함수.
    function cancelConfirmation() {
        manualSortRequested = false
        previewController.cancelSort()
        showStep(confirmationReturnStep)
    }
    Connections {
        target: previewController
        // 완료된 검사만 요청 당시의 미리보기 또는 최종 확인 준비로 연결하는 함수.
        function onPreviewFinished(success) {
            const action = home.previewAction
            home.previewAction = 0
            if (!success || !home.visible) return
            if (action === 1) home.showStep(1)
            if (action === 2 && previewController.hasMovableSteps) home.requestSort()
        }
        // 이 페이지에서 요청한 수동 정리만 진행 화면으로 연결하는 함수.
        function onSortExecutionStarted() {
            if (!home.manualSortRequested || !home.visible) return
            home.manualSession = true
            home.manualSortRequested = false
            home.showStep(3)
        }
        // 결과 반영이 끝난 알림에서 성공·실패를 구분해 완료 화면으로 연결하는 함수.
        function onSortExecutionFinished(success, message) {
            if (!home.manualSession) return
            home.manualSession = false
            home.sortSuccess = success
            home.resultMessage = message
            home.showStep(4)
        }
        // 최종 확인을 취소한 요청은 나중의 다른 작업에 연결되지 않도록 해제하는 함수.
        function onPreviewChanged() {
            // 작업 시작 알림과 결과 반영이 끝난 뒤 취소·준비 실패 여부를 확인하는 함수.
            Qt.callLater(function() {
                if (previewController.awaitingConfirmation && home.manualSortRequested && home.visible)
                    home.showStep(2)
                else if (!backgroundTasks.busy && !previewController.awaitingConfirmation && home.workflowStep < 3) {
                    if (home.workflowStep === 2) home.showStep(home.confirmationReturnStep)
                    home.manualSortRequested = false
                }
            })
        }
    }
    // 파일 정리를 벗어나면 임시 미리보기만 닫고 대기 목록·입력은 보존하는 함수.
    onVisibleChanged: {
        if (!visible) previewAction = 0
        if (!visible && workflowStep === 2) cancelConfirmation()
        if (!visible && workflowStep < 3) { manualSortRequested = false; showStep(0) }
    }
    signal pageRequested(int page)
    signal recoveryRequested()
    readonly property var theme: UiTheme.colors(dark)
    readonly property color foreground: theme.text
    readonly property color muted: theme.muted
    readonly property color surface: theme.surface
    readonly property color edge: theme.border
    color: theme.surface

    FileDialog {
        id: addFilesDialog
        title: qsTr("정리할 파일 선택")
        fileMode: FileDialog.OpenFiles
        onAccepted: { for (const url of selectedFiles) pendingFiles.addUrl(url) }
    }
    FolderDialog {
        id: destinationDialog
        title: qsTr("정리할 폴더 선택")
        onAccepted: {
            destination.text = destinationSettings.folderPath(selectedFolder)
            previewController.clear()
        }
    }

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
            border.color: control.hovered || control.visualFocus ? home.theme.menuFocus : home.edge
            border.width: control.hovered || control.visualFocus ? 2 : 1
            opacity: control.enabled ? 1 : 0.6
        }
    }
    ScrollView {
        id: homeScroll
        objectName: "fileSortScroll"
        enabled: home.workflowStep === 0 && !home.transitioning
        visible: home.paneVisible(0)
        x: UiMetrics.pageInset + home.paneX(0)
        y: UiMetrics.pageInset
        width: home.width - UiMetrics.pageInset * 2
        height: home.height - UiMetrics.pageInset * 2
        contentWidth: availableWidth
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: homeScroll.availableWidth
            spacing: UiMetrics.spaceControl
            ColumnLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceSmall
                Label { text: qsTr("파일 정리"); font.pixelSize: UiMetrics.textPageTitle; font.bold: true; color: home.foreground }
                Label { Layout.fillWidth: true; text: qsTr("파일을 담고, 결과를 확인한 뒤 원하는 위치로 정리하세요."); wrapMode: Text.Wrap; color: home.muted }
            }
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("대기 목록"); color: home.foreground; font.bold: true }
                StatusBadge { text: qsTr("%1개").arg(pendingFiles.count); dark: home.dark }
                Item { Layout.fillWidth: true }
                FlatButton { objectName: "addPendingFilesButton"; text: qsTr("＋ 파일 추가"); onClicked: addFilesDialog.open() }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(160, Math.min(240, home.height - 485))
                color: home.surface; radius: UiMetrics.radiusCard; border.color: home.edge
                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width - UiMetrics.editorInset * 2
                    visible: pendingFiles.count === 0
                    spacing: UiMetrics.spaceSmall
                    Label { Layout.fillWidth: true; text: qsTr("이곳에 파일을 놓아주세요"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; horizontalAlignment: Text.AlignHCenter; color: home.foreground }
                    Label { Layout.fillWidth: true; text: qsTr("위의 파일 추가 버튼으로도 선택할 수 있어요.\n최종 확인 전까지 원본은 그대로 유지돼요."); horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: home.muted }
                }
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
                                Text { Layout.fillWidth: true; text: available ? fileName : qsTr("[없음] %1").arg(fileName); elide: Text.ElideMiddle; color: available ? home.foreground : home.theme.danger; font.pixelSize: UiMetrics.textBody }
                                Text { Layout.fillWidth: true; text: fullPath; elide: Text.ElideMiddle; color: home.muted; font.pixelSize: UiMetrics.textBody }
                            }
                            FlatButton { text: "×"; implicitWidth: UiMetrics.iconButtonSize; Accessible.name: qsTr("목록에서 빼기"); onClicked: pendingFiles.removeAt(index) }
                        }
                    }
                }
            }
            Label { Layout.fillWidth: true; visible: pendingFiles.storageNotice.length > 0; text: pendingFiles.storageNotice; wrapMode: Text.Wrap; color: home.theme.danger; font.pixelSize: UiMetrics.textBody }
            Label { Layout.fillWidth: true; text: pendingFiles.lastMessage; visible: text.length > 0; wrapMode: Text.Wrap; color: home.muted }
            Label { text: qsTr("분류 규칙"); color: home.muted; font.pixelSize: UiMetrics.textBody }
            FlatButton {
                Layout.fillWidth: true
                text: previewController.activeRuleLabel + "  ›"
                contentItem: Text { text: parent.text; elide: Text.ElideMiddle; color: home.foreground; font.pixelSize: UiMetrics.textBody; verticalAlignment: Text.AlignVCenter }
                onClicked: home.pageRequested(1)
            }
            Label { text: qsTr("정리할 위치"); color: home.muted; font.pixelSize: UiMetrics.textBody }
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceControl
                AppTextField {
                id: destination
                objectName: "compactDestinationField"
                Layout.fillWidth: true
                text: previewController.defaultDestinationRoot
                placeholderText: qsTr("정리 폴더의 전체 경로")
                font.pixelSize: UiMetrics.textBody
                color: home.foreground
                onTextEdited: previewController.clear()
                Accessible.name: qsTr("정리할 폴더 경로")
                }
                FlatButton { objectName: "chooseDestinationButton"; text: qsTr("폴더 선택"); onClicked: destinationDialog.open() }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                LargeActionButton {
                    objectName: "compactPreviewButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    text: qsTr("결과 미리보기")
                    enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0
                    // 결과만 만들고 같은 창 안에 임시 화면을 표시하는 함수.
                    onClicked: { home.previewAction = 1; previewController.createPreview(destination.text) }
                }
                LargeActionButton {
                    objectName: "compactSortButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    primary: true
                    text: qsTr("정리하기")
                    enabled: pendingFiles.count > 0 && pendingFiles.unavailableCount === 0 && !previewController.awaitingConfirmation
                    // 미리보기·준비 후 기존 최종 확인을 거쳐야만 실제 파일 이동을 시작하는 함수.
                    onClicked: {
                        home.previewAction = 2
                        previewController.createPreview(destination.text)
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
            Label { Layout.fillWidth: true; visible: pendingFiles.unavailableCount > 0; text: qsTr("찾을 수 없는 파일을 목록에서 빼주세요."); wrapMode: Text.Wrap; color: home.theme.danger; font.pixelSize: UiMetrics.textBody }
            FlatButton { text: qsTr("중단 작업 확인"); visible: recoveryController.recoveryCases.length > 0; onClicked: home.recoveryRequested() }
        }
    }
    DropArea {
        id: pageDrop
        objectName: "fileSortPageDropArea"
        anchors.fill: parent
        z: 10
        enabled: home.visible && home.enabled && home.workflowStep === 0 && !home.transitioning
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
    FileSortPreview {
        x: home.paneX(1)
        width: home.width
        height: home.height
        visible: home.paneVisible(1)
        enabled: home.previewOpen && !home.transitioning
        controller: previewController
        dark: home.dark
        onBackRequested: home.showStep(0)
        // 확인한 미리보기를 기존 준비·승인 절차로 전달하는 함수.
        onSortRequested: home.requestSort()
    }
    FileSortConfirmation {
        x: home.paneX(2)
        width: home.width
        height: home.height
        visible: home.paneVisible(2)
        enabled: home.workflowStep === 2 && !home.transitioning
        controller: previewController
        dark: home.dark
        onCancelRequested: home.cancelConfirmation()
        // 확인 버튼만 기존 승인·실행 계약을 호출하는 함수.
        onConfirmRequested: previewController.confirmSort()
    }
    Repeater {
        model: [3, 4]
        delegate: FileSortStatus {
            required property int modelData
            objectName: modelData === 3 ? "fileSortProgress" : "fileSortComplete"
            x: home.paneX(modelData)
            width: home.width
            height: home.height
            visible: home.paneVisible(modelData)
            enabled: home.workflowStep === modelData && !home.transitioning
            completed: modelData === 4
            success: home.sortSuccess
            resultMessage: home.resultMessage
            controller: previewController
            progress: backgroundTasks
            dark: home.dark
            onBackRequested: home.showStep(0)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: home.visible && [1, 2, 4].includes(home.workflowStep) && !backgroundTasks.busy
        // Esc는 확장 창을 접지 않고 임시 미리보기만 닫는 함수.
        onActivated: home.workflowStep === 2 ? home.cancelConfirmation() : home.showStep(0)
    }
}

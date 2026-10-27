import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 파일 대기 목록·정리 위치·정리 요청은 홈과 분리된 페이지에서만 표시한다.
Rectangle {
    id: home
    required property var controller
    required property var pending
    required property var recovery
    required property var progress
    objectName: "fileSortPage"
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    property int workflowStep: 0
    property int departingStep: 0
    property real transitionProgress: 1
    property bool manualSortRequested: false
    property int confirmationReturnStep: 0
    property bool manualSession: false
    property bool sortSuccess: false
    property string resultMessage: ""
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
        home.controller.prepareSort()
        manualSortRequested = home.progress.busy || home.controller.awaitingConfirmation
        if (home.controller.awaitingConfirmation) showStep(2)
    }
    // 최종 확인을 취소하고 준비 전 입력 또는 미리보기로 돌아가는 함수.
    function cancelConfirmation() {
        manualSortRequested = false
        home.controller.cancelSort()
        showStep(confirmationReturnStep)
    }
    Connections {
        target: home.controller
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
                if (home.controller.awaitingConfirmation && home.manualSortRequested && home.visible)
                    home.showStep(2)
                else if (!home.progress.busy && !home.controller.awaitingConfirmation && home.workflowStep < 3) {
                    if (home.workflowStep === 2) home.showStep(home.confirmationReturnStep)
                    home.manualSortRequested = false
                }
            })
        }
    }
    // 파일 정리를 벗어나면 임시 미리보기만 닫고 대기 목록·입력은 보존하는 함수.
    onVisibleChanged: {
        if (!visible && workflowStep === 2) cancelConfirmation()
        if (!visible && workflowStep < 3) { manualSortRequested = false; showStep(0) }
    }
    signal pageRequested(int page)
    signal recoveryRequested()
    readonly property color foreground: theme.text
    readonly property color muted: theme.muted
    readonly property color surface: theme.surface
    readonly property color edge: theme.border
    color: theme.surface

    FileSortStepper {
        id: stepper
        anchors.left: parent.left
        anchors.right: parent.right
        workflowStep: home.workflowStep
        dark: home.dark
    }
    FileSortInput {
        enabled: home.workflowStep === 0 && !home.transitioning
        visible: home.paneVisible(0)
        x: home.paneX(0)
        y: stepper.height
        width: home.width
        height: home.height - stepper.height
        controller: home.controller
        pending: home.pending
        recovery: home.recovery
        dark: home.dark
        onRulesRequested: home.pageRequested(1)
        onRecoveryRequested: home.recoveryRequested()
        onUndoRequested: home.controller.undoLastMove()
        // 결과만 만들고 같은 창 안에 임시 화면을 표시하는 함수.
        onPreviewRequested: function(destination) {
            home.controller.createPreview(destination)
            home.showStep(1)
        }
        // 미리보기·검사·최종 확인을 거친 요청만 이동 실행으로 전달하는 함수.
        onSortRequested: function(destination) {
            home.controller.createPreview(destination)
            if (home.controller.hasPreview && home.controller.hasMovableSteps) home.requestSort()
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
                for (const url of event.urls) home.pending.addUrl(url)
                event.accept(Qt.CopyAction)
            } else event.accepted = false
        }
        Rectangle {
            objectName: "fileDropOverlay"
            anchors.fill: parent
            visible: pageDrop.containsDrag
            color: Qt.rgba(UiTheme.white.r, UiTheme.white.g, UiTheme.white.b, UiMetrics.dropOverlayOpacity)
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
        y: stepper.height
        height: home.height - stepper.height
        visible: home.paneVisible(1)
        enabled: home.previewOpen && !home.transitioning
        controller: home.controller
        dark: home.dark
        onBackRequested: home.showStep(0)
        // 확인한 미리보기를 기존 준비·승인 절차로 전달하는 함수.
        onSortRequested: home.requestSort()
    }
    FileSortConfirmation {
        x: home.paneX(2)
        width: home.width
        y: stepper.height
        height: home.height - stepper.height
        visible: home.paneVisible(2)
        enabled: home.workflowStep === 2 && !home.transitioning
        controller: home.controller
        dark: home.dark
        onCancelRequested: home.cancelConfirmation()
        // 확인 버튼만 기존 승인·실행 계약을 호출하는 함수.
        onConfirmRequested: home.controller.confirmSort()
    }
    Repeater {
        model: [3, 4]
        delegate: FileSortStatus {
            required property int modelData
            objectName: modelData === 3 ? "fileSortProgress" : "fileSortComplete"
            x: home.paneX(modelData)
            width: home.width
            y: stepper.height
            height: home.height - stepper.height
            visible: home.paneVisible(modelData)
            enabled: home.workflowStep === modelData && !home.transitioning
            completed: modelData === 4
            success: home.sortSuccess
            resultMessage: home.resultMessage
            controller: home.controller
            progress: home.progress
            dark: home.dark
            onBackRequested: home.showStep(0)
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: home.visible && [1, 2, 4].includes(home.workflowStep) && !home.progress.busy
        // Esc는 확장 창을 접지 않고 임시 미리보기만 닫는 함수.
        onActivated: home.workflowStep === 2 ? home.cancelConfirmation() : home.showStep(0)
    }
}

import QtQuick

// 복구 조회 준비와 확인창·진행 표시 연결을 파일 정리 기능 안에 모은 화면.
PageSurface {
    id: page
    required property var controller
    required property var progress
    required property var guideService
    required property Item overlayHost
    property bool initialized: false
    Component.onCompleted: {
        initialized = true
        if (visible) controller.refreshRecoveryCases()
    }
    onVisibleChanged: if (initialized && visible) controller.refreshRecoveryCases()

    // 조회는 화면에 맡기되 복구 작업의 실행이나 승인은 시작하지 않는 함수.
    function open() {
        if (visible) controller.refreshRecoveryCases()
        openRequested()
    }
    RecoveryPage {
        anchors.fill: parent
        anchors.margins: UiMetrics.pageInset
        controller: page.controller
        overlayHost: page.overlayHost
        busy: page.progress.busy
        inspecting: page.visible && page.progress.busy && !page.progress.fileWorkActive
        dark: page.dark
        onOpenRequested: page.open()
        onCloseRequested: page.closeRequested()
    }
}

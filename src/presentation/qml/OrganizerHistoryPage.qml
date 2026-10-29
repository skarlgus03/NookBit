import QtQuick

// 기록 조회 준비와 진행 상태·확인창 부모를 기능 소유 화면에 연결하는 페이지.
PageSurface {
    id: page
    required property var controller
    required property var progress
    required property var guideService
    required property Item overlayHost
    property bool initialized: false
    Component.onCompleted: {
        initialized = true
        if (visible) controller.refreshHistory()
    }
    onVisibleChanged: if (initialized && visible) controller.refreshHistory()

    // 같은 기록 페이지를 다시 요청해도 최신 기록을 조회하고 열기를 요청하는 함수.
    function open() {
        if (visible) controller.refreshHistory()
        openRequested()
    }
    HistoryPage {
        objectName: "historyDialog"
        anchors.fill: parent
        controller: page.controller
        overlayHost: page.overlayHost
        busy: page.progress.busy
        dark: page.dark
        onOpenRequested: page.open()
        onCloseRequested: page.closeRequested()
    }
}

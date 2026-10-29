import QtQuick

// 정리 화면과 호스트의 페이지 전환·진행 표시 사이를 잇는 기능 소유 어댑터.
PageSurface {
    id: page
    required property var controller
    required property var pending
    required property var recovery
    required property var progress
    required property var guideService
    required property Item overlayHost
    readonly property int workflowStep: sorting.workflowStep
    readonly property bool manualSortRequested: sorting.manualSortRequested

    FileSortPage {
        id: sorting
        anchors.fill: parent
        controller: page.controller
        pending: page.pending
        recovery: page.recovery
        progress: page.progress
        dark: page.dark
        onPageRequested: function(route) { page.routeRequested(route) }
        onRecoveryRequested: page.routeRequested(10)
    }
}

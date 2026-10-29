import QtQuick

// 감시 입력 화면의 준비·닫기 요청을 공통 설정 슬라이드에 연결하는 기능 소유 페이지.
PageSurface {
    id: page
    required property var controller
    required property var guideService
    required property Item overlayHost
    WatchSettingsPage {
        anchors.fill: parent
        controller: page.controller
        dark: page.dark
        onOpenRequested: page.openRequested()
        onCloseRequested: page.closeRequested()
    }
}

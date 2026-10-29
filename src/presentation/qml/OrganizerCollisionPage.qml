import QtQuick

// 기능 소유 충돌 설정 창구를 공통 페이지 서비스에 연결하는 화면.
PageSurface {
    id: page
    required property var controller
    required property var guideService
    required property Item overlayHost
    CollisionSettingsPage {
        anchors.fill: parent
        controller: page.controller
        dark: page.dark
        onOpenRequested: page.openRequested()
        onCloseRequested: page.closeRequested()
    }
}

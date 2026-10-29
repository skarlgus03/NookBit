import QtQuick

// 알림 목록과 표시 정책 변경을 같은 기능의 창구에 연결하는 페이지.
PageSurface {
    id: page
    required property var controller
    required property var guideService
    required property Item overlayHost
    NotificationSettingsPage {
        anchors.fill: parent
        anchors.margins: UiMetrics.editorInset
        dark: page.dark
        types: page.controller.types
        status: page.controller.status
        onEnabledRequested: function(typeId, enabled) { page.controller.setEnabled(typeId, enabled) }
    }
}

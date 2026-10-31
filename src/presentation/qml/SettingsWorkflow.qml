import QtQuick

// 공통 설정과 등록된 상세 화면을 같은 공간에서 전환하며 기능 입력은 소유하지 않는 흐름.
Item {
    id: workflow
    required property var settings
    property var watchContribution: ({})
    property var detailPage: ({})
    property var guideService: null
    property Item overlayHost: parent
    property bool active: false
    property bool detailOpen: false
    property var organizerContribution: ({})
    property bool dark: false
    property var startupMessages: []
    property string applicationVersion: ""
    readonly property bool transitioning: slide.running
    property real slidePosition: detailOpen ? 1 : 0
    signal settingsRequested()
    signal routeRequested(int page)
    signal dialogRequested(string dialogId)
    signal tutorialRequested()
    clip: true
    Behavior on slidePosition {
        NumberAnimation { id: slide; duration: workflow.active ? UiMetrics.pageSlideDuration : 0; easing.type: Easing.OutCubic }
    }
    AppSettingsPage {
        width: workflow.width
        height: workflow.height
        x: -workflow.width * workflow.slidePosition
        visible: workflow.active && (!workflow.detailOpen || workflow.transitioning)
        enabled: !workflow.detailOpen && !workflow.transitioning
        settings: workflow.settings
        organizerContribution: workflow.organizerContribution
        startupMessages: workflow.startupMessages
        applicationVersion: workflow.applicationVersion
        watchContribution: workflow.watchContribution
        dark: workflow.dark
        onRouteRequested: function(route) { workflow.routeRequested(route) }
        onTutorialRequested: workflow.tutorialRequested()
    }
    RegisteredPageHost {
        registration: workflow.detailPage
        guideService: workflow.guideService
        overlayHost: workflow.overlayHost
        width: workflow.width
        height: workflow.height
        x: workflow.width * (1 - workflow.slidePosition)
        selected: workflow.active && (workflow.detailOpen || workflow.transitioning)
        enabled: workflow.detailOpen && !workflow.transitioning
        dark: workflow.dark
        onPageRequested: function(route) { workflow.routeRequested(route) }
        onNavigationRequested: function(route) { workflow.routeRequested(route) }
        onDialogRequested: function(dialogId) { workflow.dialogRequested(dialogId) }
        onCloseRequested: workflow.settingsRequested()
    }
    Shortcut {
        sequence: "Escape"
        enabled: workflow.enabled && workflow.active && workflow.detailOpen && !workflow.transitioning
        onActivated: workflow.settingsRequested()
    }
}

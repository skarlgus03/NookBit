import QtQuick

// 설정과 감시 입력을 같은 공간에서 전환하며 창 크기나 설정값은 소유하지 않는 화면 흐름.
Item {
    id: workflow
    required property var settings
    required property var destinations
    required property var sorting
    required property var watch
    property bool active: false
    property bool watchOpen: false
    property bool recoveryAvailable: false
    property bool dark: false
    readonly property bool transitioning: slide.running
    property real slidePosition: watchOpen ? 1 : 0
    signal watchRequested()
    signal settingsRequested()
    signal collisionRequested()
    signal recoveryRequested()
    clip: true
    Behavior on slidePosition {
        NumberAnimation { id: slide; duration: workflow.active ? UiMetrics.pageSlideDuration : 0; easing.type: Easing.OutCubic }
    }
    AppSettingsPage {
        width: workflow.width
        height: workflow.height
        x: -workflow.width * workflow.slidePosition
        visible: workflow.active && (!workflow.watchOpen || workflow.transitioning)
        enabled: !workflow.watchOpen && !workflow.transitioning
        settings: workflow.settings
        destinations: workflow.destinations
        sorting: workflow.sorting
        watch: workflow.watch
        recoveryAvailable: workflow.recoveryAvailable
        dark: workflow.dark
        onWatchSettingsRequested: workflow.watchRequested()
        onCollisionSettingsRequested: workflow.collisionRequested()
        onRecoveryRequested: workflow.recoveryRequested()
    }
    WatchSettingsPage {
        width: workflow.width
        height: workflow.height
        x: workflow.width * (1 - workflow.slidePosition)
        visible: workflow.active && (workflow.watchOpen || workflow.transitioning)
        enabled: workflow.watchOpen && !workflow.transitioning
        controller: workflow.watch
        dark: workflow.dark
        onOpenRequested: workflow.watchRequested()
        onCloseRequested: workflow.settingsRequested()
    }
    Shortcut {
        sequence: "Escape"
        enabled: workflow.enabled && workflow.active && workflow.watchOpen && !workflow.transitioning
        onActivated: workflow.settingsRequested()
    }
}

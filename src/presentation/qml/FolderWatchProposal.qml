import QtQuick

// 제안창의 수락·미루기를 기능 창구에 연결하고 수락 완료만 호스트에 알리는 표시기.
Item {
    id: surface
    required property var controller
    required property Item overlayHost
    property bool busy: false
    property bool dark: false
    readonly property bool opened: dialog.opened
    signal accepted()
    // 실제 파일 이동 없이 기존 제안 확인창을 여는 함수.
    function open() { dialog.open() }
    // 제안 사실이나 대기 목록을 바꾸지 않고 확인창을 닫는 함수.
    function close() { dialog.close() }
    WatchProposalDialog {
        id: dialog
        objectName: "watchProposalDialog"
        parent: surface.overlayHost
        controller: surface.controller
        busy: surface.busy
        onPendingAccepted: surface.accepted()
    }
}

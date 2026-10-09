import QtQuick
import QtQuick.Window

// 파일 작업과 별개로 캐릭터 표현 값만 재생하는 투명 독립 창.
Window {
    id: pet
    objectName: "desktopCharacterWindow"
    required property var placement
    required property var descriptor
    required property string message
    property real userScale: 0.5
    signal settingsRequested()
    transientParent: null
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    title: qsTr("NookBit 캐릭터")
    visible: placement.shown
    readonly property var bounds: frameBounds()
    width: Math.max(180, Math.ceil(bounds.right - bounds.left) + 32)
    height: Math.max(100, Math.ceil(bounds.bottom - bounds.top) + 76)
    x: placement.anchorX - sprite.anchor.x
    y: placement.anchorY - sprite.anchor.y
    // 닫기 명령은 앱 종료가 아니라 캐릭터 숨기기로 처리하는 함수.
    onClosing: function(event) { event.accepted = false; placement.setShown(false) }
    onXChanged: positionCommit.restart()
    onYChanged: positionCommit.restart()

    // 마지막 이동 결과를 화면 피벗 좌표로 확정하는 함수.
    function commitPosition() {
        placement.moveAnchor(Math.round(x + sprite.anchor.x), Math.round(y + sprite.anchor.y))
    }

    // 애니메이션 전체 포즈의 피벗 기준 영역을 구해 프레임마다 창이 흔들리지 않게 하는 함수.
    function frameBounds() {
        let left = -40, top = -48, right = 40, bottom = 16
        const steps = sprite.steps
        for (let i = 0; i < steps.length; ++i) {
            const step = steps[i]
            if (!step.region || !step.pivot || !(step.pixelsPerUnit > 0)) continue
            const l = -step.pivot.x * userScale, t = -step.pivot.y * userScale
            const r = l + step.region.width * userScale / step.pixelsPerUnit
            const b = t + step.region.height * userScale / step.pixelsPerUnit
            if (!isFinite(l) || !isFinite(t) || !isFinite(r) || !isFinite(b)) continue
            left = Math.min(left, l); top = Math.min(top, t)
            right = Math.max(right, r); bottom = Math.max(bottom, b)
        }
        // 비정상적으로 큰 팩의 좌표로 거대한 네이티브 창을 만들지 않는다.
        return {left: Math.max(-1024, left), top: Math.max(-1024, top),
            right: Math.min(1024, right), bottom: Math.min(1024, bottom)}
    }

    CharacterSprite {
        id: sprite
        objectName: "desktopCharacterSprite"
        anchors.fill: parent
        descriptor: pet.descriptor
        userScale: pet.userScale
        anchor: Qt.point((pet.width - (pet.bounds.right - pet.bounds.left)) / 2 - pet.bounds.left,
                         32 - pet.bounds.top)
        playing: pet.visible
    }
    Text {
        anchors.centerIn: parent
        visible: !sprite.ready
        text: sprite.error.length ? sprite.error : qsTr("캐릭터")
        color: "#ffffff"
        style: Text.Outline
        styleColor: "#263246"
    }
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // 왼쪽 버튼은 창 이동, 오른쪽 버튼은 설정 열기로 처리하는 함수.
        onPressed: function(mouse) {
            if (mouse.button === Qt.LeftButton) pet.startSystemMove()
            else pet.settingsRequested()
        }
    }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: pet.height - 36
        width: Math.min(pet.width - 8, 320)
        height: 30
        radius: 10
        color: "#e6263246"
        Text {
            anchors.fill: parent
            anchors.margins: 6
            text: pet.message
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: "#edf2f8"
            font.pixelSize: 12
        }
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 4
        width: 24; height: 24; radius: 12
        color: "#c0263246"
        Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: 18 }
        MouseArea {
            anchors.fill: parent
            // 캐릭터만 숨기는 함수.
            onClicked: pet.placement.setShown(false)
        }
    }
    Timer {
        id: positionCommit
        interval: 180
        // 운영체제의 창 이동 결과를 화면 피벗 좌표로 저장하는 함수.
        onTriggered: pet.commitPosition()
    }
}

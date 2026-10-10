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
    property bool dark: true
    property var bubbleSettings: ({})
    // 캐릭터 부착점은 이미지 피벗 기준 논리 좌표이며 개인 배율을 적용한다.
    readonly property point bubbleOffset: bubbleSettings.attachment
        ? Qt.point(bubbleSettings.attachment.x, bubbleSettings.attachment.y) : Qt.point(0, -120)
    property bool actionAvailable: false
    property bool testingBubble: false
    signal settingsRequested()
    signal actionRequested()
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
    // 정리 제안이나 캐릭터 상태를 바꾸지 않고 말풍선 배치만 시험하는 함수.
    function testBubble() {
        testingBubble = true
        bubbleTestDeadline.restart()
    }
    CharacterBubble {
        dark: pet.dark
        configuration: pet.bubbleSettings
        transientParent: pet
        visible: pet.visible && message.length > 0
        screenArea: pet.placement.screenArea
        anchorPoint: Qt.point(pet.x + sprite.anchor.x + pet.bubbleOffset.x * pet.userScale,
                              pet.y + sprite.anchor.y + pet.bubbleOffset.y * pet.userScale)
        message: pet.testingBubble ? qsTr("말풍선 위치 테스트예요. 캐릭터를 화면 가장자리로 옮겨보세요!") : pet.message
        actionAvailable: pet.testingBubble || pet.actionAvailable
        actionText: pet.testingBubble ? qsTr("닫기") : qsTr("확인")
        onActionRequested: {
            if (pet.testingBubble) pet.testingBubble = false
            else pet.actionRequested()
        }
        // 말풍선 닫기로 앱이 종료되지 않도록 캐릭터 표시만 해제하는 함수.
        onClosing: function(event) { event.accepted = false; pet.placement.setShown(false) }
    }
    Timer { id: bubbleTestDeadline; interval: 10000; onTriggered: pet.testingBubble = false }

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

import QtQuick

// 검증된 격자 시트에서 현재 프레임만 보여주는 재생기. 기능 상태나 파일 작업은 모른다.
Item {
    id: sprite
    property var descriptor: ({})
    property bool playing: true
    property int sequenceIndex: 0
    property bool finished: false
    readonly property bool metadataValid: descriptor && descriptor.source !== undefined
        && descriptor.columns > 0 && descriptor.rows > 0
        && descriptor.frameWidth > 0 && descriptor.frameHeight > 0
        && descriptor.frames && descriptor.frames.length > 0 && descriptor.frameDurationMs >= 16
    readonly property int currentFrame: metadataValid
        ? descriptor.frames[Math.min(sequenceIndex, descriptor.frames.length - 1)] : 0
    readonly property bool ready: metadataValid && atlas.status === Image.Ready
        && atlas.sourceSize.width === descriptor.frameWidth * descriptor.columns
        && atlas.sourceSize.height === descriptor.frameHeight * descriptor.rows
    readonly property string error: !metadataValid ? ""
        : atlas.status === Image.Error ? qsTr("스프라이트 이미지를 읽지 못했어요.")
        : atlas.status === Image.Ready && !ready ? qsTr("스프라이트 이미지 크기가 격자 설정과 달라요.") : ""
    implicitWidth: 64
    implicitHeight: 64
    clip: true

    // 새 재생 설정을 받으면 첫 프레임과 미완료 상태로 돌아가는 함수.
    function restart() {
        sequenceIndex = 0
        finished = false
    }
    // 지정된 순서로 다음 프레임을 선택하고 일회 재생은 마지막 프레임에 멈추는 함수.
    function advanceFrame() {
        if (!ready || finished) return
        if (sequenceIndex + 1 < descriptor.frames.length) sequenceIndex++
        else if (descriptor.loop) sequenceIndex = 0
        else finished = true
    }
    onDescriptorChanged: restart()

    Image {
        id: atlas
        objectName: "characterAtlas"
        source: sprite.metadataValid ? sprite.descriptor.source : ""
        width: sprite.width * (sprite.metadataValid ? sprite.descriptor.columns : 1)
        height: sprite.height * (sprite.metadataValid ? sprite.descriptor.rows : 1)
        x: -sprite.width * (sprite.metadataValid ? sprite.currentFrame % sprite.descriptor.columns : 0)
        y: -sprite.height * (sprite.metadataValid ? Math.floor(sprite.currentFrame / sprite.descriptor.columns) : 0)
        visible: sprite.ready
        asynchronous: true
        smooth: false
        cache: true
    }
    Timer {
        objectName: "characterSpriteTimer"
        interval: sprite.metadataValid ? sprite.descriptor.frameDurationMs : 100
        repeat: true
        running: sprite.playing && sprite.visible && sprite.ready && !sprite.finished
            && sprite.descriptor.frames.length > 1
        onTriggered: sprite.advanceFrame()
    }
}

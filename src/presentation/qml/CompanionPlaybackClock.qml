import QtQuick

// 미리보기 전용 시간 재생기. 프레임 선택과 진행 막대가 하나의 시간을 공유한다.
Item {
    id: clock
    property var steps: []
    property bool playing: false
    property bool ready: true
    property bool loop: true
    property real positionMs: 0
    property bool finished: false
    readonly property real durationMs: steps.reduce((total, step) => total + step.durationMs, 0)
    readonly property int currentIndex: frameAt(positionMs)
    signal frameChanging()

    // 지정한 시간에 보여줄 프레임 번호를 반환하는 함수.
    function frameAt(timeMs) {
        let end = 0
        for (let i = 0; i < steps.length; ++i) {
            end += steps[i].durationMs
            if (timeMs < end || i === steps.length - 1) return i
        }
        return 0
    }
    // 프레임 전환 전에 이전 그림을 보존할 기회를 알리고 시간을 변경하는 함수.
    function setPosition(timeMs) {
        if (frameAt(timeMs) !== currentIndex) frameChanging()
        positionMs = timeMs
    }
    // 지정 시각으로 이동하고 일시정지 이후의 시간 기준을 갱신하는 함수.
    function seek(timeMs) {
        if (!isFinite(timeMs)) return
        setPosition(Math.max(0, Math.min(durationMs, timeMs)))
        finished = durationMs > 0 && positionMs >= durationMs
        ticker.previousTime = Date.now()
    }
    // 선택한 프레임의 시작 시각으로 이동하는 함수.
    function seekFrame(index) {
        let start = 0
        for (let i = 0; i < Math.min(index, steps.length - 1); ++i) start += steps[i].durationMs
        seek(start)
    }
    // 경과 시간만큼 진행하고 반복 또는 마지막 위치 정지를 처리하는 함수.
    function advance(elapsedMs) {
        if (!isFinite(elapsedMs) || elapsedMs <= 0 || durationMs <= 0 || finished) return
        const next = positionMs + elapsedMs
        if (next >= durationMs && !loop) {
            setPosition(durationMs)
            finished = true
        } else setPosition(loop ? next % durationMs : next)
    }
    onStepsChanged: seek(0)
    Timer {
        id: ticker
        property real previousTime: 0
        interval: 16
        repeat: true
        running: clock.visible && clock.playing && clock.ready && !clock.finished && clock.durationMs > 0
        onRunningChanged: previousTime = Date.now()
        // 타이머 호출 횟수 대신 실제 경과 시간으로 재생 위치를 갱신하는 함수.
        onTriggered: {
            const now = Date.now()
            clock.advance(Math.max(0, now - previousTime))
            previousTime = now
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 프레임의 유지 시간을 비례 폭과 키 표시로 보여주는 읽기 전용 시간 트랙.
Rectangle {
    id: timeline
    required property var steps
    required property var theme
    property int currentIndex: 0
    property string filtering: "nearest"
    property bool followPlayback: false
    readonly property real pixelsPerMs: UiMetrics.companionTimelinePixelsPerSecond / 1000
    readonly property var entries: buildEntries(steps)
    readonly property real durationMs: entries.length ? entries[entries.length - 1].endMs : 0
    property real positionMs: 0
    readonly property real currentTimeMs: Math.max(0, Math.min(durationMs, positionMs))
    signal timeSelected(real timeMs)
    color: theme.surface
    radius: UiMetrics.radiusControl
    border.color: theme.border
    implicitHeight: UiMetrics.companionTimelineHeight

    // 재생 위치가 트랙 밖으로 나가면 현재 프레임이 보이도록 따라가는 함수.
    function followCurrentFrame() {
        if (!followPlayback || durationMs <= 0) return
        const position = currentTimeMs * pixelsPerMs
        if (position < track.contentX || position > track.contentX + track.width - UiMetrics.spaceSection)
            track.contentX = Math.max(0, Math.min(track.contentWidth - track.width, position - track.width / 2))
    }
    onCurrentTimeMsChanged: followCurrentFrame()

    // 각 프레임의 시작 시간과 종료 시간을 누적하여 반환하는 함수.
    function buildEntries(values) {
        let elapsed = 0
        return values.map(function(step, index) {
            const start = elapsed
            elapsed += step.durationMs
            return {step: step, index: index, startMs: start, endMs: elapsed}
        })
    }
    // 트랙의 선택 시각을 재생 길이 안으로 제한해서 전달하는 함수.
    function selectTime(timeMs) {
        if (!entries.length || !isFinite(timeMs)) return
        timeSelected(Math.max(0, Math.min(durationMs, timeMs)))
    }
    Flickable {
        id: track
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceControl
        clip: true
        // 애니메이션 길이나 창 크기가 바뀌어도 1초의 표시 폭은 유지한다.
        contentWidth: Math.max(width, (Math.ceil(timeline.durationMs / UiMetrics.companionTimelineTickMs) + 1) * UiMetrics.companionTimelineTickMs * timeline.pixelsPerMs)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar {}
        Repeater {
            // 긴 애니메이션에서도 현재 보이는 눈금만 생성한다.
            model: Math.ceil(track.width / (UiMetrics.companionTimelineTickMs * timeline.pixelsPerMs)) + 2
            delegate: Item {
                required property int index
                readonly property int tickIndex: Math.floor(track.contentX / (UiMetrics.companionTimelineTickMs * timeline.pixelsPerMs)) + index
                x: tickIndex * UiMetrics.companionTimelineTickMs * timeline.pixelsPerMs
                height: track.height
                Rectangle { width: UiMetrics.borderNormal; height: parent.height; color: timeline.theme.border }
                Label {
                    x: UiMetrics.spaceSmall
                    text: Number((tickIndex * UiMetrics.companionTimelineTickMs / 1000).toFixed(1)) + " s"
                    color: timeline.theme.muted
                    font.pixelSize: UiMetrics.textBody
                }
            }
        }
        Repeater {
            model: timeline.entries
            delegate: Rectangle {
                id: cell
                required property var modelData
                readonly property bool selected: modelData.index === timeline.currentIndex
                x: modelData.startMs * timeline.pixelsPerMs
                y: UiMetrics.companionTimelineRulerHeight
                width: modelData.step.durationMs * timeline.pixelsPerMs
                height: track.height - y - UiMetrics.spaceSection
                color: selected ? timeline.theme.menuSelected : timeline.theme.alternate
                border.color: selected ? timeline.theme.primaryFocus : timeline.theme.border
                border.width: selected ? UiMetrics.borderActive : UiMetrics.borderNormal
                Rectangle {
                    width: UiMetrics.companionTimelineKeySize; height: width
                    x: width / 2; y: -height / 2
                    rotation: 45
                    color: cell.selected ? timeline.theme.primaryFocus : timeline.theme.muted
                }
                Column {
                    anchors.fill: parent
                    anchors.margins: UiMetrics.spaceSmall
                    visible: cell.width >= UiMetrics.companionFrameWidth
                    CharacterSprite {
                        width: parent.width; height: UiMetrics.companionFrameImageHeight
                        descriptor: ({steps: [cell.modelData.step], filtering: timeline.filtering})
                        playing: false
                        userScale: Math.min(width / (cell.modelData.step.region.width / cell.modelData.step.pixelsPerUnit), height / (cell.modelData.step.region.height / cell.modelData.step.pixelsPerUnit))
                        anchor: Qt.point((width - cell.modelData.step.region.width / cell.modelData.step.pixelsPerUnit * userScale) / 2 + cell.modelData.step.pivot.x * userScale,
                            (height - cell.modelData.step.region.height / cell.modelData.step.pixelsPerUnit * userScale) / 2 + cell.modelData.step.pivot.y * userScale)
                    }
                    Label {
                        width: parent.width
                        text: (cell.modelData.index + 1) + " · " + cell.modelData.step.durationMs + " ms"
                        color: timeline.theme.text; elide: Text.ElideRight
                        font.pixelSize: UiMetrics.textBody
                    }
                }
            }
        }
        Rectangle {
            objectName: "companionPlayhead"
            x: timeline.currentTimeMs * timeline.pixelsPerMs
            width: UiMetrics.borderActive
            height: track.height - UiMetrics.spaceControl
            color: timeline.theme.primaryFocus
            Rectangle {
                width: UiMetrics.companionTimelineKeySize
                height: width
                x: (parent.width - width) / 2
                rotation: 45
                color: timeline.theme.primaryFocus
            }
        }
        MouseArea {
            width: track.contentWidth; height: track.height
            cursorShape: Qt.SizeHorCursor
            // 시간 눈금 위를 누르거나 끌어서 해당 프레임으로 이동하는 함수.
            function seek(mouse) { timeline.selectTime(Math.max(0, mouse.x / timeline.pixelsPerMs)) }
            onPressed: function(mouse) { seek(mouse) }
            onPositionChanged: function(mouse) { if (pressed) seek(mouse) }
        }
    }
}

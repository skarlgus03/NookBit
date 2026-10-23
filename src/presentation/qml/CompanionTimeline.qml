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
    readonly property var entries: buildEntries(steps)
    readonly property real durationMs: entries.length ? entries[entries.length - 1].endMs : 0
    readonly property real currentTimeMs: entries.length && currentIndex >= 0 && currentIndex < entries.length ? entries[currentIndex].startMs : 0
    signal frameSelected(int index)
    color: theme.surface
    radius: UiMetrics.radiusControl
    border.color: theme.border
    implicitHeight: UiMetrics.companionTimelineHeight

    // 재생 위치가 트랙 밖으로 나가면 현재 프레임이 보이도록 따라가는 함수.
    function followCurrentFrame() {
        if (!followPlayback || durationMs <= 0) return
        const position = track.contentWidth * currentTimeMs / durationMs
        if (position < track.contentX || position > track.contentX + track.width - UiMetrics.spaceSection)
            track.contentX = Math.max(0, Math.min(track.contentWidth - track.width, position - track.width / 2))
    }
    onCurrentIndexChanged: followCurrentFrame()

    // 각 프레임의 시작 시간과 종료 시간을 누적하여 반환하는 함수.
    function buildEntries(values) {
        let elapsed = 0
        return values.map(function(step, index) {
            const start = elapsed
            elapsed += step.durationMs
            return {step: step, index: index, startMs: start, endMs: elapsed}
        })
    }
    // 지정한 시간에 해당하는 프레임을 선택하는 함수.
    function selectTime(timeMs) {
        if (!entries.length) return
        for (let i = 0; i < entries.length; ++i) {
            if (timeMs < entries[i].endMs || i === entries.length - 1) {
                frameSelected(i)
                return
            }
        }
    }
    Flickable {
        id: track
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceControl
        clip: true
        contentWidth: Math.max(width, timeline.durationMs / Math.max(1, timeline.steps.reduce((minimum, frame) => Math.min(minimum, frame.durationMs), Infinity)) * UiMetrics.companionTimelineKeySize * 2)
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar {}
        Repeater {
            model: timeline.durationMs > 0 ? 6 : 0
            delegate: Item {
                required property int index
                x: track.contentWidth * index / 5
                height: track.height
                Rectangle { width: UiMetrics.borderNormal; height: parent.height; color: timeline.theme.border }
                Label {
                    x: index === 5 ? -width : UiMetrics.spaceSmall
                    text: (timeline.durationMs * index / 5 / 1000).toFixed(2) + " s"
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
                x: timeline.durationMs > 0 ? track.contentWidth * modelData.startMs / timeline.durationMs : 0
                y: UiMetrics.companionTimelineRulerHeight
                width: timeline.durationMs > 0 ? track.contentWidth * modelData.step.durationMs / timeline.durationMs : 0
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
            x: timeline.durationMs > 0 ? Math.min(track.contentWidth - width, track.contentWidth * timeline.currentTimeMs / timeline.durationMs) : 0
            width: UiMetrics.borderActive
            height: track.height - UiMetrics.spaceControl
            color: timeline.theme.primaryFocus
        }
        MouseArea {
            width: track.contentWidth; height: track.height
            // 시간 눈금 위를 누르거나 끌어서 해당 프레임으로 이동하는 함수.
            function seek(mouse) { timeline.selectTime(Math.max(0, mouse.x / Math.max(1, track.contentWidth) * timeline.durationMs)) }
            onPressed: function(mouse) { seek(mouse) }
            onPositionChanged: function(mouse) { if (pressed) seek(mouse) }
        }
    }
}

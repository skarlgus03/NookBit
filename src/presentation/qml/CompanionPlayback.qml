import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 화면이나 컨트롤러를 참조하지 않고 재생 조작과 시간 탐색 요청을 전달하는 부품.
Rectangle {
    id: playback
    required property var steps
    required property var theme
    property string filtering: "nearest"
    property int currentIndex: 0
    property bool playing: false
    property bool finished: false
    property real positionMs: 0
    property bool editable: false
    property bool snapEnabled: true
    readonly property real durationMs: timeline.durationMs
    readonly property real currentTimeMs: timeline.currentTimeMs
    signal toggleRequested()
    signal frameSelected(int index)
    signal timeSelected(real timeMs)
    signal durationEditStarted()
    signal durationChanged(int index, real durationMs)
    color: theme.surface
    border.color: theme.border
    radius: UiMetrics.radiusPanel
    implicitHeight: contents.implicitHeight + UiMetrics.spaceGroup * 2

    ColumnLayout {
        id: contents
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceGroup
        spacing: UiMetrics.spaceControl
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("재생"); color: playback.theme.text; font.pixelSize: UiMetrics.textSectionTitle; font.bold: true }
            AppButton {
                objectName: "companionTimelineSnapButton"
                visible: playback.editable
                text: qsTr("스냅 · %1초").arg(UiMetrics.companionTimelineSnapMs / 1000)
                checkable: true
                checked: playback.snapEnabled
                onClicked: playback.snapEnabled = checked
                ToolTip.visible: hovered
                ToolTip.text: qsTr("시간 눈금에 맞추기 · Alt를 누르면 잠시 해제")
            }
            Item { Layout.fillWidth: true }
            Label {
                objectName: "companionPlaybackStatus"
                text: !playback.steps.length ? qsTr("프레임 없음") : playback.finished ? qsTr("재생 완료") : playback.playing ? qsTr("● 재생 중") : qsTr("일시정지")
                font.pixelSize: UiMetrics.textEmphasis
                font.bold: true
                color: playback.theme.text
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: UiMetrics.spaceControl
            PlaybackIconButton {
                objectName: "companionFirstFrameButton"
                kind: "first"; description: qsTr("처음 프레임")
                enabled: playback.steps.length > 0
                onClicked: playback.frameSelected(0)
            }
            PlaybackIconButton {
                objectName: "companionPlaybackButton"
                kind: playback.playing ? "pause" : "play"
                description: playback.playing ? qsTr("일시정지") : qsTr("재생")
                enabled: playback.steps.length > 0
                onClicked: playback.toggleRequested()
            }
            PlaybackIconButton {
                objectName: "companionLastFrameButton"
                kind: "last"; description: qsTr("마지막 프레임")
                enabled: playback.steps.length > 0
                onClicked: playback.frameSelected(playback.steps.length - 1)
            }
            Item { Layout.fillWidth: true }
            Label {
                text: playback.steps.length ? qsTr("프레임 %1 / %2\n%3 s / %4 s").arg(playback.currentIndex + 1).arg(playback.steps.length).arg((playback.currentTimeMs / 1000).toFixed(2)).arg((playback.durationMs / 1000).toFixed(2)) : ""
                horizontalAlignment: Text.AlignRight
                font.pixelSize: UiMetrics.textEmphasis
                color: playback.theme.text
            }
        }
        CompanionTimeline {
            id: timeline
            objectName: "companionFrameTimeline"
            Layout.fillWidth: true
            Layout.preferredHeight: UiMetrics.companionTimelineHeight
            steps: playback.steps
            theme: playback.theme
            filtering: playback.filtering
            currentIndex: playback.currentIndex
            followPlayback: playback.playing
            positionMs: playback.positionMs
            editable: playback.editable
            snapEnabled: playback.snapEnabled
            onTimeSelected: function(timeMs) { playback.timeSelected(timeMs) }
            onDurationEditStarted: playback.durationEditStarted()
            onDurationChanged: function(index, durationMs) { playback.durationChanged(index, durationMs) }
        }
    }
}

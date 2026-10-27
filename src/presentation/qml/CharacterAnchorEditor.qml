import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 바탕화면과 독립된 정지 미리보기에서 개인 좌표만 편집하는 창.
Dialog {
    id: editor
    objectName: "characterAnchorEditor"
    required property var settings
    required property var descriptor
    required property var bubble
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    property point draftPivot: Qt.point(0, 0)
    property point draftBubble: Qt.point(0, 0)
    title: qsTr("캐릭터 기준점 조정")
    modal: false
    width: Math.min(parent.width - UiMetrics.spaceSection, UiMetrics.characterAnchorWidth)
    height: Math.min(parent.height - UiMetrics.spaceSection, UiMetrics.characterAnchorHeight)
    x: (parent.width - width) / 2
    y: (parent.height - height) / 2
    // 저장된 개인 보정값을 초안으로 복사하는 함수.
    function reload() { draftPivot = settings.pivotOffset; draftBubble = settings.bubbleOffset }
    onOpened: reload()
    Connections {
        target: editor.settings
        // 팩 선택 변경 시 이전 초안을 버리는 함수.
        function onSelected() { editor.reload() }
    }
    contentItem: ColumnLayout {
        spacing: UiMetrics.spaceGroup
        Label { text: qsTr("점을 드래그해 조정하세요. 적용 전에는 바탕화면이 바뀌지 않아요."); wrapMode: Text.Wrap; Layout.fillWidth: true }
        Item {
            id: stage
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            readonly property var step: preview.currentStep
            readonly property real zoom: preview.metadataValid ? Math.min(width * 0.6 / (step.region.width / step.pixelsPerUnit), height * 0.65 / (step.region.height / step.pixelsPerUnit)) : 1
            readonly property point base: preview.metadataValid ? Qt.point((width - step.region.width / step.pixelsPerUnit * zoom) / 2 + step.pivot.x * zoom, (height - step.region.height / step.pixelsPerUnit * zoom) / 2 + step.pivot.y * zoom) : Qt.point(width / 2, height / 2)
            readonly property var attachment: editor.bubble.attachment || ({x: 0, y: -120})
            CharacterSprite {
                id: preview
                anchors.fill: parent
                descriptor: editor.descriptor
                playing: false
                userScale: stage.zoom
                anchor: stage.base
            }
            Repeater {
                model: 2
                delegate: Item {
                    id: marker
                    required property int index
                    readonly property point offset: index === 0 ? editor.draftPivot : Qt.point(stage.attachment.x + editor.draftBubble.x, stage.attachment.y + editor.draftBubble.y)
                    x: stage.base.x + offset.x * stage.zoom
                    y: stage.base.y + offset.y * stage.zoom
                    Rectangle { x: -marker.x; width: stage.width; height: 1; color: marker.index === 0 ? editor.theme.pivotGuide : editor.theme.bubbleGuide; opacity: 0.65 }
                    Label { x: UiMetrics.spaceGroup; y: -height - UiMetrics.spaceExtraSmall; text: marker.index === 0 ? qsTr("캐릭터 기준점") : qsTr("말풍선 부착점") }
                    Rectangle {
                        width: UiMetrics.characterAnchorHandle; height: width; radius: width / 2
                        x: -width / 2; y: -height / 2
                        color: marker.index === 0 ? editor.theme.pivotGuide : editor.theme.bubbleGuide
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.SizeAllCursor
                            // 커서 위치를 논리 좌표로 변환해 초안만 조정하는 함수.
                            function updatePoint(mouse) {
                                const p = mapToItem(stage, mouse.x, mouse.y)
                                const dx = Math.max(-1024, Math.min(1024, (p.x - stage.base.x) / stage.zoom - (marker.index === 1 ? stage.attachment.x : 0)))
                                const dy = Math.max(-1024, Math.min(1024, (p.y - stage.base.y) / stage.zoom - (marker.index === 1 ? stage.attachment.y : 0)))
                                if (marker.index === 0) editor.draftPivot = Qt.point(dx, dy)
                                else editor.draftBubble = Qt.point(dx, dy)
                            }
                            onPositionChanged: function(mouse) { if (pressed) updatePoint(mouse) }
                        }
                    }
                }
            }
        }
        RowLayout {
            AppButton { text: qsTr("기본값 복원"); onClicked: { editor.draftPivot = Qt.point(0, 0); editor.draftBubble = Qt.point(0, 0) } }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("취소"); onClicked: editor.close() }
            AppButton { text: qsTr("적용"); onClicked: { editor.settings.setPlacement(editor.draftPivot, editor.draftBubble); editor.close() } }
        }
    }
}

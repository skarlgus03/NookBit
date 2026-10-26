import QtQuick
import QtQuick.Controls

// 정확한 중심과 넓은 드래그 영역을 구분하는 공통 기준점 핸들.
Item {
    id: handle
    required property color markerColor
    required property color backdropColor
    required property string description
    property bool diamond: false
    signal dragged(real localX, real localY)
    width: UiMetrics.characterAnchorHitSize
    height: width
    ToolTip.visible: dragArea.containsMouse || dragArea.pressed
    ToolTip.text: description
    Canvas {
        id: graphic
        anchors.centerIn: parent
        width: UiMetrics.characterAnchorVisualSize
        height: width
        Connections {
            target: handle
            // 기준점 종류나 테마가 바뀌면 표시를 다시 그리는 함수.
            function onMarkerColorChanged() { graphic.requestPaint() }
            function onBackdropColorChanged() { graphic.requestPaint() }
            function onDiamondChanged() { graphic.requestPaint() }
        }
        // 십자 교차점과 중앙 점으로 실제 부착 위치를 명확히 그리는 함수.
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.scale(width / 24, height / 24)
            ctx.beginPath()
            ctx.moveTo(12, 1); ctx.lineTo(12, 23)
            ctx.moveTo(1, 12); ctx.lineTo(23, 12)
            ctx.strokeStyle = handle.backdropColor
            ctx.lineWidth = UiMetrics.borderActive * 2
            ctx.stroke()
            ctx.strokeStyle = handle.markerColor
            ctx.lineWidth = UiMetrics.borderActive
            ctx.stroke()
            ctx.beginPath()
            if (handle.diamond) {
                ctx.moveTo(12, 7); ctx.lineTo(17, 12); ctx.lineTo(12, 17); ctx.lineTo(7, 12); ctx.closePath()
            } else ctx.rect(8, 8, 8, 8)
            ctx.fillStyle = handle.backdropColor
            ctx.fill(); ctx.stroke()
            ctx.fillStyle = handle.markerColor
            ctx.fillRect(11, 11, 2, 2)
        }
    }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true
        preventStealing: true
        cursorShape: Qt.SizeAllCursor
        // 핸들 안의 포인터 위치를 상위 편집 화면에 전달하는 함수.
        onPositionChanged: function(mouse) { if (pressed) handle.dragged(mouse.x, mouse.y) }
    }
}

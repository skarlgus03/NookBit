import QtQuick

// 글꼴 기호 대신 같은 선 모양으로 그리는 공통 드롭다운 화살표.
Item {
    id: icon
    property color strokeColor: UiTheme.light.text
    implicitWidth: UiMetrics.dropdownIconWidth
    implicitHeight: UiMetrics.dropdownIconHeight
    width: implicitWidth
    height: implicitHeight
    onStrokeColorChanged: drawing.requestPaint()
    Canvas {
        id: drawing
        anchors.fill: parent
        // 테마에 맞는 두 선분을 일정한 굵기로 그리는 함수.
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.strokeStyle = icon.strokeColor
            ctx.lineWidth = UiMetrics.borderActive
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const inset = UiMetrics.borderActive / 2
            ctx.beginPath(); ctx.moveTo(inset, inset); ctx.lineTo(width / 2, height - inset * 2); ctx.lineTo(width - inset, inset); ctx.stroke()
        }
    }
}

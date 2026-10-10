import QtQuick

// 글꼴 기호 대신 같은 선 모양으로 그리는 공통 드롭다운 화살표.
Item {
    id: icon
    property color strokeColor: "#273344"
    implicitWidth: 12
    implicitHeight: 8
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
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            ctx.beginPath(); ctx.moveTo(1, 1); ctx.lineTo(6, 6); ctx.lineTo(11, 1); ctx.stroke()
        }
    }
}

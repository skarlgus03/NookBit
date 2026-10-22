import QtQuick

// 메뉴 종류를 같은 선 굵기와 크기로 표시하는 코드 기반 아이콘.
Canvas {
    id: icon
    property string kind: "folder"
    property color strokeColor: "#273344"
    implicitWidth: UiMetrics.navigationIconSize
    implicitHeight: UiMetrics.navigationIconSize
    onKindChanged: requestPaint()
    onStrokeColorChanged: requestPaint()
    // 공통 좌표계에서 파일·규칙·기록·설정 아이콘을 그리는 함수.
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        ctx.clearRect(0, 0, width, height)
        ctx.scale(width / 24, height / 24)
        ctx.strokeStyle = strokeColor
        ctx.lineWidth = UiMetrics.borderActive
        ctx.lineCap = "round"; ctx.lineJoin = "round"
        ctx.beginPath()
        if (kind === "folder") {
            ctx.moveTo(3, 7); ctx.lineTo(3, 20); ctx.lineTo(21, 20); ctx.lineTo(21, 8); ctx.lineTo(11, 8); ctx.lineTo(9, 5); ctx.lineTo(3, 5); ctx.lineTo(3, 7)
        } else if (kind === "rules") {
            ctx.rect(2, 9, 6, 6); ctx.rect(16, 2, 6, 6); ctx.rect(16, 16, 6, 6)
            ctx.moveTo(8, 12); ctx.lineTo(12, 12); ctx.lineTo(12, 5); ctx.lineTo(16, 5)
            ctx.moveTo(12, 12); ctx.lineTo(12, 19); ctx.lineTo(16, 19)
        } else if (kind === "companion") {
            ctx.arc(12, 12, 9, 0, Math.PI * 2)
            ctx.moveTo(8, 9); ctx.lineTo(8, 11)
            ctx.moveTo(16, 9); ctx.lineTo(16, 11)
            ctx.moveTo(8, 15); ctx.quadraticCurveTo(12, 19, 16, 15)
        } else if (kind === "history") {
            ctx.arc(12, 12, 9, 0, Math.PI * 2)
            ctx.moveTo(12, 6); ctx.lineTo(12, 12); ctx.lineTo(16, 15)
        } else {
            for (let row = 0; row < 3; ++row) {
                const y = 5 + row * 7
                const x = row === 1 ? 15 : 8
                ctx.moveTo(3, y); ctx.lineTo(x - 2, y); ctx.moveTo(x + 2, y); ctx.lineTo(21, y); ctx.rect(x - 2, y - 2, 4, 4)
            }
        }
        ctx.stroke()
    }
}

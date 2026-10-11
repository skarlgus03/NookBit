import QtQuick
import QtQuick.Controls

// 모든 페이지에서 같은 화살표·클릭 영역·접근성 이름을 쓰는 돌아가기 버튼.
AppButton {
    id: control
    property string description: qsTr("뒤로")
    implicitWidth: UiMetrics.controlHeight
    implicitHeight: UiMetrics.controlHeight
    Accessible.name: description
    ToolTip.visible: hovered
    ToolTip.text: description
    contentItem: Item {
        Canvas {
            id: arrow
            anchors.centerIn: parent
            width: UiMetrics.iconSize
            height: UiMetrics.iconSize
            Connections {
                target: control
                // 테마가 변경되면 화살표를 다시 그리는 함수.
                function onPaletteChanged() { arrow.requestPaint() }
            }
            // 글꼴 기호 대신 같은 모양의 뒤로 화살표를 그리는 함수.
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = control.palette.buttonText
                ctx.lineWidth = UiMetrics.borderActive
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(width * 0.45, height * 0.2)
                ctx.lineTo(width * 0.15, height * 0.5)
                ctx.lineTo(width * 0.45, height * 0.8)
                ctx.moveTo(width * 0.15, height * 0.5)
                ctx.lineTo(width * 0.85, height * 0.5)
                ctx.stroke()
            }
        }
    }
}

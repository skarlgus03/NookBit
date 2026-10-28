import QtQuick
import QtQuick.Controls

// 글꼴에 의존하지 않는 공통 재생 조작 아이콘 버튼.
AppButton {
    id: control
    required property string kind
    required property string description
    implicitWidth: UiMetrics.controlHeight
    implicitHeight: UiMetrics.controlHeight
    Accessible.name: description
    ToolTip.visible: hovered
    ToolTip.text: description
    contentItem: Item {
        Canvas {
            id: symbol
            anchors.centerIn: parent
            width: UiMetrics.iconSize; height: width
            readonly property color ink: control.enabled ? control.palette.buttonText : control.palette.disabled.buttonText
            onInkChanged: requestPaint()
            Connections {
                target: control
                // 재생 상태 변경 시 아이콘을 다시 그리는 함수.
                function onKindChanged() { symbol.requestPaint() }
            }
            // 같은 좌표계로 처음·재생·일시정지·마지막 아이콘을 그리는 함수.
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.scale(width / 24, height / 24)
                ctx.fillStyle = ink
                if (control.kind === "pause") {
                    ctx.fillRect(6, 4, 4, 16); ctx.fillRect(14, 4, 4, 16)
                } else {
                    if (control.kind === "first") {
                        ctx.fillRect(4, 4, 3, 16)
                        ctx.beginPath(); ctx.moveTo(19, 4); ctx.lineTo(8, 12); ctx.lineTo(19, 20)
                    } else {
                        if (control.kind === "last") ctx.fillRect(17, 4, 3, 16)
                        ctx.beginPath(); ctx.moveTo(5, 4); ctx.lineTo(16, 12); ctx.lineTo(5, 20)
                    }
                    ctx.closePath(); ctx.fill()
                }
            }
        }
    }
}

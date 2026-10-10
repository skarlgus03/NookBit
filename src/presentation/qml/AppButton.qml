import QtQuick.Controls

// 일반 명령 버튼은 공통 높이·글씨·아이콘 규격을 사용한다.
Button {
    implicitHeight: UiMetrics.controlHeight
    font.pixelSize: UiMetrics.textBody
    icon.width: UiMetrics.iconSize
    icon.height: UiMetrics.iconSize
}

import QtQuick.Controls

// 숫자 입력 상자도 같은 높이·글씨 크기로 표시한다.
SpinBox {
    implicitHeight: UiMetrics.controlHeight
    font.pixelSize: UiMetrics.textBody
}

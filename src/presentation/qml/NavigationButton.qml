import QtQuick

// 페이지 이동 버튼의 공통 규격과 상태 피드백을 표시한다.
AppButton {
    id: control
    padding: UiMetrics.buttonPadding
    background: ButtonFeedback {
        button: control
        normalColor: control.buttonTheme.navigationButton
        selectedColor: control.buttonTheme.navigationSelected
    }
}

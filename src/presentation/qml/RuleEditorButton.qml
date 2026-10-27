import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 편집기 명령의 줄바꿈·정렬과 공통 버튼 피드백을 표시한다.
AppButton {
    id: themedButton
    property int labelAlignment: Text.AlignHCenter
    contentItem: Text {
        text: themedButton.text
        font: themedButton.font
        color: themedButton.buttonForeground
        horizontalAlignment: themedButton.labelAlignment
        verticalAlignment: Text.AlignVCenter
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 이미지 왼쪽 위 기준의 두 점을 숫자로 정확히 지정하는 편집 팝업.
Popup {
    id: editor
    objectName: "companionCoordinateEditor"
    property point pivot: Qt.point(0, 0)
    property point attachment: Qt.point(0, 0)
    signal submitted(real px, real py, real ax, real ay)
    width: Math.min(UiMetrics.companionCoordinateEditorWidth, parent.width - UiMetrics.spaceSection * 2)
    height: contentItem.implicitHeight + topPadding + bottomPadding
    x: (parent.width - width) / 2
    y: Math.max(0, (parent.height - height) / 2)
    padding: UiMetrics.spaceSection
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // 현재 좌표를 소수점 세 자리까지 입력 상자에 채우는 함수.
    onOpened: {
        pivotX.text = Number(pivot.x.toFixed(3)).toString();
        pivotY.text = Number(pivot.y.toFixed(3)).toString();
        attachmentX.text = Number(attachment.x.toFixed(3)).toString();
        attachmentY.text = Number(attachment.y.toFixed(3)).toString();
        pivotX.forceActiveFocus();
    }
    component CoordinateField: AppTextField {
        Layout.fillWidth: true
        validator: DoubleValidator {
            bottom: -4096
            top: 4096
            decimals: 3
            notation: DoubleValidator.StandardNotation
            locale: "C"
        }
    }
    contentItem: ColumnLayout {
        spacing: UiMetrics.spaceGroup
        Label {
            text: qsTr("좌표 입력 · 이미지 왼쪽 위 기준")
            font.bold: true
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: UiMetrics.spaceControl
            rowSpacing: UiMetrics.spaceControl
            Item {}
            Label {
                text: "X"
            }
            Label {
                text: "Y"
            }
            Label {
                text: qsTr("기준점")
            }
            CoordinateField {
                id: pivotX
            }
            CoordinateField {
                id: pivotY
            }
            Label {
                text: qsTr("말풍선")
            }
            CoordinateField {
                id: attachmentX
            }
            CoordinateField {
                id: attachmentY
            }
        }
        RowLayout {
            Item {
                Layout.fillWidth: true
            }
            AppButton {
                text: qsTr("닫기")
                onClicked: editor.close()
            }
            AppButton {
                text: qsTr("초안에 반영")
                enabled: pivotX.acceptableInput && pivotY.acceptableInput && attachmentX.acceptableInput && attachmentY.acceptableInput
                onClicked: {
                    editor.submitted(Number(pivotX.text), Number(pivotY.text), Number(attachmentX.text), Number(attachmentY.text));
                    editor.close();
                }
            }
        }
    }
}

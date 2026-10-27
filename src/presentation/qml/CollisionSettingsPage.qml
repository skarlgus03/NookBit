import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 이름 충돌 정책을 표시하고 기존 정리 Controller에 변경을 전달하는 페이지.
PageSurface {
    id: page
    required property var controller
    ColumnLayout {
        anchors.fill: parent
        Label {
            text: qsTr("같은 이름의 파일이 이미 있으면")
            font.bold: true
        }
        AppComboBox {
            textRole: "label"
            valueRole: "key"
            model: [
                {
                    label: qsTr("이름 변경"),
                    key: "rename"
                },
                {
                    label: qsTr("건너뛰기"),
                    key: "skip"
                },
                {
                    label: qsTr("작업 중단"),
                    key: "abort"
                }
            ]
            currentIndex: page.controller.collisionPolicy === "skip" ? 1 : page.controller.collisionPolicy === "abort"
                                                                       ? 2 : 0
            onActivated: page.controller.collisionPolicy = currentValue
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정이 우선합니다.")
            wrapMode: Text.Wrap
        }
        Item {
            Layout.fillHeight: true
        }
    }
}

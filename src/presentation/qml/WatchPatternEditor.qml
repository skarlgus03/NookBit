import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 제외 패턴을 파일에 접근하지 않고 칩과 쉼표 목록으로 편집하는 입력.
Item {
    id: editor
    property string text: ""
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property var patterns: text.split(",").map(value => value.trim()).filter(value => value.length > 0)
    readonly property bool pending: entry.text.trim().length > 0
    implicitHeight: contents.implicitHeight
    // 입력 중인 패턴은 Enter 또는 저장 시 목록에 반영하는 함수.
    function commitPending() {
        const values = patterns.slice()
        for (const value of entry.text.split(",").map(value => value.trim()).filter(value => value.length > 0))
            if (!values.includes(value)) values.push(value)
        if (pending) text = values.join(", ")
        entry.clear()
    }
    // 되돌리기에서 아직 확정하지 않은 입력도 비우는 함수.
    function clearPending() { entry.clear() }
    ColumnLayout {
        id: contents
        width: parent.width
        spacing: UiMetrics.spaceControl
        Flow {
            id: chips
            Layout.fillWidth: true
            visible: editor.patterns.length > 0
            spacing: UiMetrics.spaceControl
            Repeater {
                model: editor.patterns
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    width: Math.min(chips.width, patternText.implicitWidth + UiMetrics.floatingCloseSize + UiMetrics.spaceControl * 3)
                    height: UiMetrics.controlHeight
                    color: editor.theme.softButton
                    radius: UiMetrics.radiusControl
                    Label {
                        id: patternText
                        anchors.left: parent.left
                        anchors.right: remove.left
                        anchors.margins: UiMetrics.spaceControl
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: editor.theme.accentSoftText
                        font.pixelSize: UiMetrics.textBody
                        HoverHandler { id: chipHover }
                        ToolTip.visible: chipHover.hovered
                        ToolTip.text: modelData
                    }
                    AppButton {
                        id: remove
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: UiMetrics.floatingCloseSize
                        leftPadding: UiMetrics.spaceExtraSmall
                        rightPadding: UiMetrics.spaceExtraSmall
                        text: "×"
                        flat: true
                        dark: editor.dark
                        Accessible.name: qsTr("%1 패턴 제거").arg(modelData)
                        onClicked: editor.text = editor.patterns.filter((value, position) => position !== index).join(", ")
                    }
                }
            }
        }
        AppTextField {
            id: entry
            objectName: "watchPatternInput"
            Layout.fillWidth: true
            placeholderText: qsTr("패턴 추가 후 Enter · 여러 개는 쉼표로 구분")
            Accessible.name: qsTr("제외 패턴 추가")
            onAccepted: editor.commitPending()
        }
    }
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs

Dialog {
    id: guide
    property string guideKey: "rules"
    property bool dark: false
    title: guideKey === "rules" ? qsTr("분류 규칙 사용 가이드") : qsTr("캐릭터팩 사용·제작 가이드")
    modal: true
    anchors.centerIn: parent
    width: Math.min(parent.width - UiMetrics.spaceSection * 2, UiMetrics.guideWidth)
    height: Math.min(parent.height - UiMetrics.spaceSection * 2, UiMetrics.guideHeight)
    padding: UiMetrics.spaceSection
    palette.window: dark ? "#202633" : "#ffffff"
    palette.windowText: dark ? "#edf2f8" : "#27352b"
    palette.text: palette.windowText
    palette.buttonText: palette.windowText
    onOpened: {
        userGuides.clearError()
        guideScroll.contentItem.contentY = 0
    }
    background: Rectangle {
        radius: UiMetrics.radiusPanel
        color: guide.palette.window
        border.color: guide.dark ? "#3d4757" : "#d9dfd8"
    }
    contentItem: ColumnLayout {
        spacing: UiMetrics.spaceGroup
        ScrollView {
            id: guideScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            TextArea {
                objectName: "userGuideText"
                width: guideScroll.availableWidth
                readOnly: true
                selectByMouse: true
                textFormat: TextEdit.MarkdownText
                wrapMode: TextEdit.Wrap
                text: userGuides.text(guide.guideKey)
                color: guide.palette.text
                font.family: UiMetrics.fontFamily
                font.pixelSize: UiMetrics.textEmphasis
                padding: UiMetrics.spaceControl
                background: null
            }
        }
        Label {
            Layout.fillWidth: true
            visible: userGuides.error !== ""
            text: userGuides.error
            color: guide.palette.text
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            AppButton { text: qsTr("문서 저장…"); onClicked: guideSaveDialog.open() }
            Label { Layout.fillWidth: true; text: qsTr("인터넷 없이 읽을 수 있어요."); color: guide.palette.text; wrapMode: Text.Wrap }
            AppButton { text: qsTr("닫기"); onClicked: guide.close() }
        }
    }
    NativeDialogs.FileDialog {
        id: guideSaveDialog
        title: qsTr("사용 가이드 저장")
        fileMode: NativeDialogs.FileDialog.SaveFile
        nameFilters: [qsTr("Markdown 문서 (*.md)")]
        defaultSuffix: "md"
        onAccepted: userGuides.save(guide.guideKey, selectedFile)
    }
}

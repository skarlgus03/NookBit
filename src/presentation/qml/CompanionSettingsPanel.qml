import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 주입된 표시 설정과 팩 선택만 편집하며 미리보기·가져오기 창은 상위 페이지에 요청한다.
Rectangle {
    id: panel
    required property var settings
    required property var placement
    required property var motion
    property bool canWalk: false
    property bool dirty: false
    property bool showBubble: true
    property var portrait: ({})
    property string guideStatus: ""
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    signal bubbleToggled(bool checked)
    signal importRequested()
    signal guideRequested()
    signal anchorsRequested()
    color: theme.surface
    radius: UiMetrics.radiusCard
    component Heading: Label {
        Layout.fillWidth: true
        color: panel.theme.muted
        font.pixelSize: UiMetrics.textBody
        font.bold: true
    }
    component Divider: Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: UiMetrics.spaceControl
        Layout.bottomMargin: UiMetrics.spaceControl
        implicitHeight: UiMetrics.borderNormal
        color: panel.theme.border
    }
    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: UiMetrics.spaceSection
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ColumnLayout {
            width: scroll.availableWidth
            spacing: UiMetrics.spaceControl
            Heading { text: qsTr("캐릭터 팩") }
            AppComboBox {
                id: packChoice
                objectName: "companionPackChoice"
                Layout.fillWidth: true
                implicitHeight: UiMetrics.companionPackSelectorHeight
                model: panel.settings.packs
                textRole: "label"
                valueRole: "key"
                currentIndex: {
                    const items = panel.settings.packs
                    return items.findIndex(pack => pack.key === panel.settings.selectedKey)
                }
                enabled: !panel.settings.busy && !panel.dirty
                onActivated: panel.settings.selectPack(currentValue)
                contentItem: RowLayout {
                    property bool truncated: packText.truncated
                    spacing: UiMetrics.spaceControl
                    CompanionPortrait {
                        Layout.preferredWidth: UiMetrics.companionPackPortraitSize
                        Layout.preferredHeight: UiMetrics.companionPackPortraitSize
                        descriptor: panel.portrait
                        dark: panel.dark
                    }
                    Label {
                        id: packText
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: packChoice.displayText
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: UiMetrics.textBody
                        font.bold: true
                        color: panel.theme.text
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceControl
                AppButton {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    appearance: AppButton.Soft
                    dark: panel.dark
                    text: qsTr("팩 가져오기")
                    enabled: !panel.settings.busy && !panel.dirty
                    onClicked: panel.importRequested()
                }
                AppButton {
                    objectName: "characterUserGuideButton"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    flat: true
                    dark: panel.dark
                    text: qsTr("제작 가이드 저장")
                    onClicked: panel.guideRequested()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: panel.guideStatus.length > 0
                text: panel.guideStatus
                wrapMode: Text.Wrap
                font.pixelSize: UiMetrics.textBody
                color: panel.theme.muted
            }
            Divider {}
            Heading { text: qsTr("표시") }
            AppSwitch {
                objectName: "companionDesktopToggle"
                Layout.fillWidth: true
                dark: panel.dark
                text: qsTr("바탕화면에 표시")
                checked: panel.placement.shown
                onToggled: panel.placement.setShown(checked)
            }
            AppSwitch {
                Layout.fillWidth: true
                dark: panel.dark
                text: qsTr("쉬는 동안 가끔 걷기")
                checked: panel.motion.enabled
                enabled: panel.canWalk
                onToggled: panel.motion.setEnabled(checked)
            }
            AppSwitch {
                objectName: "companionBubbleToggle"
                Layout.fillWidth: true
                dark: panel.dark
                text: qsTr("미리보기에 말풍선 표시")
                checked: panel.showBubble
                onToggled: panel.bubbleToggled(checked)
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: qsTr("표시 크기"); color: panel.theme.text; font.pixelSize: UiMetrics.textBody }
                Label { text: qsTr("%1%").arg(Math.round(panel.settings.userScale * 100)); color: panel.theme.text; font.pixelSize: UiMetrics.textBody; font.bold: true }
            }
            AppSlider {
                objectName: "companionDisplayScale"
                Layout.fillWidth: true
                dark: panel.dark
                from: 0.1
                to: 2.0
                stepSize: 0.05
                value: panel.settings.userScale
                onMoved: panel.settings.setUserScale(value)
                Accessible.name: qsTr("바탕화면 컴패니언 표시 크기")
            }
            RowLayout {
                Layout.fillWidth: true
                Label { Layout.fillWidth: true; text: qsTr("기본값 50%"); color: panel.theme.muted; font.pixelSize: UiMetrics.textBody }
                AppButton { text: qsTr("기본값으로"); flat: true; dark: panel.dark; onClicked: panel.settings.setUserScale(0.5) }
            }
            Divider {}
            Heading { text: qsTr("위치") }
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceControl
                AppButton { Layout.fillWidth: true; Layout.preferredWidth: 1; text: qsTr("기준점 보정"); appearance: AppButton.Soft; dark: panel.dark; onClicked: panel.anchorsRequested() }
                AppButton { Layout.fillWidth: true; Layout.preferredWidth: 1; text: qsTr("위치 되돌리기"); appearance: AppButton.Soft; dark: panel.dark; onClicked: panel.placement.resetPosition() }
            }
            Divider {}
            Heading { text: qsTr("바탕화면에서") }
            Label {
                Layout.fillWidth: true
                text: qsTr("클릭: 반응\n드래그: 이동\nShift + 드래그: 쓰다듬기\n우클릭: 상호작용 · 설정 메뉴")
                font.pixelSize: UiMetrics.textBody
                color: panel.theme.muted
                lineHeight: 1.5
            }
            Label {
                Layout.fillWidth: true
                text: panel.settings.status
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: panel.theme.muted
                font.pixelSize: UiMetrics.textBody
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("선택과 크기는 자동 저장돼요.\n팩 원본은 변경하지 않아요.")
                wrapMode: Text.Wrap
                color: panel.theme.muted
                font.pixelSize: UiMetrics.textBody
            }
        }
    }
}

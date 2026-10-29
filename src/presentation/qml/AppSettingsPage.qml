import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 현재 설정을 카드로 요약하고 저장·세부 화면 이동을 기존 창구에 전달한다.
ScrollView {
    id: page
    required property var settings
    required property var watch
    property var organizerContribution: ({})
    property bool dark: false
    readonly property var theme: UiTheme.colors(dark)
    readonly property string watchSummary: {
        const folder = watch.folder.replace(/\\/g, "/").replace(/\/+$/, "").split("/").pop()
        const count = Number(watch.countEnabled) + Number(watch.sizeEnabled) + Number(watch.ageEnabled)
        const condition = count > 1 ? qsTr("조건 %1개 중 %2 충족 시 제안").arg(count).arg(watch.combineAll ? qsTr("모두") : qsTr("하나"))
            : watch.countEnabled ? qsTr("파일 %1개 이상이면 정리 제안").arg(watch.minimumFileCount)
            : watch.sizeEnabled ? qsTr("%1 %2 이상이면 정리 제안").arg(watch.minimumTotalSize).arg(watch.totalSizeUnit)
            : watch.ageEnabled ? qsTr("%1분 경과 시 정리 제안").arg(watch.minimumAgeMinutes) : qsTr("조건을 설정해 주세요")
        return [folder || qsTr("폴더를 지정해 주세요"), condition].join(" · ")
    }
    signal watchSettingsRequested()
    signal routeRequested(int page)
    objectName: "settingsPage"
    contentWidth: availableWidth
    clip: true
    padding: UiMetrics.spaceSection
    background: Rectangle { color: page.theme.window }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.spaceSection
        RegisteredSettingsSection {
            Layout.fillWidth: true
            contribution: page.organizerContribution.notice || ({})
            dark: page.dark
            onRouteRequested: function(route) { page.routeRequested(route) }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("화면")
            dark: page.dark
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                ColumnLayout {
                    Layout.minimumWidth: 0
                    spacing: UiMetrics.spaceExtraSmall
                    Label { text: qsTr("테마"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: page.theme.text }
                    Label { text: qsTr("선택은 바로 저장돼요."); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                }
                Item { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 0 }
                Rectangle {
                    objectName: "themeChoiceGroup"
                    Layout.alignment: Qt.AlignRight
                    Layout.minimumWidth: implicitWidth
                    Layout.maximumWidth: implicitWidth
                    implicitWidth: themeButtons.implicitWidth + UiMetrics.spaceExtraSmall * 2
                    implicitHeight: UiMetrics.controlHeight + UiMetrics.spaceExtraSmall * 2
                    color: page.theme.alternate
                    radius: UiMetrics.radiusControl
                    RowLayout {
                        id: themeButtons
                        anchors.centerIn: parent
                        spacing: 0
                        AppButton {
                            objectName: "globalLightModeButton"
                            text: qsTr("라이트")
                            dark: page.dark
                            appearance: AppButton.Menu
                            selected: !page.settings.darkMode
                            onClicked: page.settings.darkMode = false
                        }
                        AppButton {
                            objectName: "globalDarkModeSwitch"
                            text: qsTr("다크")
                            dark: page.dark
                            appearance: AppButton.Menu
                            selected: page.settings.darkMode
                            onClicked: page.settings.darkMode = true
                        }
                    }
                }
            }
        }
        RegisteredSettingsSection {
            Layout.fillWidth: true
            contribution: page.organizerContribution
            leadingEntry: watchEntry
            dark: page.dark
            onRouteRequested: function(route) { page.routeRequested(route) }
        }
    }
    Component {
        id: watchEntry
        AppButton {
            id: watchButton
            objectName: "settingsWatchButton"
            Layout.fillWidth: true
            implicitHeight: Math.max(UiMetrics.fileRowHeight, implicitContentHeight + topPadding + bottomPadding)
            appearance: AppButton.Menu
            dark: page.dark
            text: qsTr("폴더 감시")
            Accessible.name: text + ". " + page.watchSummary
            ToolTip.visible: hovered
            ToolTip.text: page.watch.folder + "\n" + page.watchSummary
            onClicked: page.watchSettingsRequested()
            contentItem: RowLayout {
                spacing: UiMetrics.spaceGroup
                NavigationIcon { Layout.preferredWidth: UiMetrics.navigationIconSize; Layout.preferredHeight: UiMetrics.navigationIconSize; kind: "folder"; strokeColor: page.theme.accent }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: UiMetrics.spaceExtraSmall
                    Label { text: watchButton.text; font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: page.theme.text }
                    Label { Layout.fillWidth: true; text: page.watchSummary; textFormat: Text.PlainText; wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                }
                Label { text: page.watch.enabled ? qsTr("켜짐") : qsTr("꺼짐"); font.pixelSize: UiMetrics.textBody; font.bold: true; color: page.watch.enabled ? page.theme.success : page.theme.muted }
                NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "forward"; strokeColor: page.theme.muted }
            }
        }
    }
}

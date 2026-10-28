import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// 현재 설정을 카드로 요약하고 저장·세부 화면 이동을 기존 창구에 전달한다.
ScrollView {
    id: page
    required property var settings
    required property var destinations
    required property var sorting
    required property var watch
    property bool recoveryAvailable: false
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
    signal collisionSettingsRequested()
    signal recoveryRequested()
    objectName: "settingsPage"
    contentWidth: availableWidth
    clip: true
    padding: UiMetrics.spaceSection
    background: Rectangle { color: page.theme.window }
    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
    ColumnLayout {
        width: page.availableWidth
        spacing: UiMetrics.spaceSection
        Rectangle {
            Layout.fillWidth: true
            visible: page.recoveryAvailable
            implicitHeight: recoveryRow.implicitHeight + UiMetrics.spaceSection * 2
            color: page.theme.warningSurface
            radius: UiMetrics.radiusCard
            RowLayout {
                id: recoveryRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: UiMetrics.spaceSection
                spacing: UiMetrics.spaceGroup
                NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "alert"; strokeColor: page.theme.warning }
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("확인이 필요한 중단 작업이 있어요.\n파일을 옮기기 전에 작업 상태를 확인해 주세요.")
                    font.pixelSize: UiMetrics.textBody
                    wrapMode: Text.Wrap
                    color: page.theme.warning
                }
                AppButton { text: qsTr("확인하기"); appearance: AppButton.Surface; dark: page.dark; onClicked: page.recoveryRequested() }
            }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("화면")
            dark: page.dark
            RowLayout {
                Layout.fillWidth: true
                spacing: UiMetrics.spaceGroup
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    spacing: UiMetrics.spaceExtraSmall
                    Label { text: qsTr("테마"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: page.theme.text }
                    Label { text: qsTr("선택은 바로 저장돼요."); font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
                }
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
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("파일 정리")
            dark: page.dark
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
            Rectangle { Layout.fillWidth: true; implicitHeight: UiMetrics.borderNormal; color: page.theme.border }
            Label { text: qsTr("같은 이름의 파일이 있으면"); font.pixelSize: UiMetrics.textEmphasis; font.bold: true; color: page.theme.text }
            Label { Layout.fillWidth: true; text: qsTr("목적지에 이미 같은 이름의 파일이 있을 때"); font.pixelSize: UiMetrics.textBody; color: page.theme.muted; wrapMode: Text.Wrap }
            CollisionPolicyPicker {
                Layout.fillWidth: true
                policy: page.sorting.collisionPolicy
                dark: page.dark
                onPolicyRequested: policy => page.sorting.collisionPolicy = policy
            }
            Label { Layout.fillWidth: true; text: qsTr("규칙에 별도 처리가 지정되어 있으면 그 설정이 우선해요."); wrapMode: Text.Wrap; font.pixelSize: UiMetrics.textBody; color: page.theme.muted }
        }
        SettingsSection {
            Layout.fillWidth: true
            title: qsTr("허용한 외부 목적지")
            dark: page.dark
            Label { Layout.fillWidth: true; text: qsTr("기본 정리 폴더 밖으로 옮길 수 있는 곳"); wrapMode: Text.Wrap; color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
            Label { Layout.fillWidth: true; visible: page.destinations.allowedRoots.length === 0; text: qsTr("허용한 외부 폴더가 없어요."); wrapMode: Text.Wrap; color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
            Repeater {
                model: page.destinations.allowedRoots
                RowLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: UiMetrics.spaceGroup
                    NavigationIcon { Layout.preferredWidth: UiMetrics.iconSize; Layout.preferredHeight: UiMetrics.iconSize; kind: "folder"; strokeColor: page.theme.muted }
                    Label {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: modelData
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        color: page.theme.text
                        font.pixelSize: UiMetrics.textBody
                        HoverHandler { id: pathHover }
                        ToolTip.visible: pathHover.hovered
                        ToolTip.text: modelData
                        ToolTip.delay: UiMetrics.tooltipDelayMs
                    }
                    AppButton { text: qsTr("허용 해제"); flat: true; destructive: true; dark: page.dark; onClicked: page.destinations.revokeFolder(modelData) }
                }
            }
            Label { Layout.fillWidth: true; text: qsTr("새 외부 폴더는 분류 규칙에서 목적지를 고를 때 허용할 수 있어요."); wrapMode: Text.Wrap; color: page.theme.muted; font.pixelSize: UiMetrics.textBody }
        }
    }
}

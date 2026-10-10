pragma Singleton
import QtQuick

// 화면 부품의 역할별 공통 규격. 캔버스 좌표·창 배치·캐릭터 팩 데이터는 포함하지 않는다.
QtObject {
    readonly property int textBody: 14
    readonly property int textCaption: 14
    readonly property int textCompact: 11
    readonly property int textAction: 15
    readonly property int textEmphasis: 16
    readonly property int textWindowTitle: 17
    readonly property int textSectionTitle: 18
    readonly property int textAppTitle: 25
    readonly property int textPageTitle: 28
    readonly property int controlHeight: 40
    readonly property int primaryActionHeight: 46
    readonly property int navigationHeight: 48
    readonly property int windowButtonSize: 32
    readonly property int iconButtonSize: 32
    readonly property int buttonMinWidth: 80
    readonly property int dropdownMinWidth: 120
    readonly property int controlPadding: 12
    readonly property int buttonPadding: 8
    readonly property int dropdownTrailingPadding: 36
    readonly property int dropdownIconWidth: 12
    readonly property int dropdownIconHeight: 8
    readonly property int dropdownVisibleRows: 8
    readonly property int iconSize: 20
    readonly property int floatingCloseSize: 24
    readonly property int spaceTiny: 2
    readonly property int spaceExtraSmall: 4
    readonly property int spaceSmall: 6
    readonly property int spaceControl: 8
    readonly property int spaceCompact: 10
    readonly property int spaceGroup: 12
    readonly property int spaceSection: 16
    readonly property int pageInset: 18
    readonly property int homeInset: 20
    readonly property int editorInset: 24
    readonly property int radiusControl: 8
    readonly property int radiusInner: 6
    readonly property int radiusPanel: 10
    readonly property int radiusCard: 12
    readonly property int borderNormal: 1
    readonly property int borderActive: 2

    // 규칙 캔버스의 글씨·블록 크기는 독립적인 표시 프리셋으로 관리한다.
    readonly property var nodeTextSizes: [11, 13, 16]
    readonly property var nodeWidths: [206, 226, 262]
    readonly property var nodeHeights: [82, 90, 104]
    readonly property int nodeCaptionMinimum: 10
    readonly property int nodeCaptionOffset: 2
    readonly property int nodeHeadingOffset: 3
    readonly property int nodeSymbolOffset: 5
    readonly property int nodeBadgeOffset: 1
}

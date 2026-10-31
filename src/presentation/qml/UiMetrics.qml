pragma Singleton
import QtQuick

// 화면 부품의 역할별 공통 규격. 캔버스 좌표·창 배치·캐릭터 팩 데이터는 포함하지 않는다.
QtObject {
    readonly property string fontFamily: "NookBit UI Sans"
    readonly property string fontFallback: "Malgun Gothic"
    readonly property list<string> fontFiles: [
        ":/nookbit/fonts/NookBitUISans-Regular.otf",
        ":/nookbit/fonts/NookBitUISans-Medium.otf",
        ":/nookbit/fonts/NookBitUISans-Bold.otf"
    ]
    readonly property int bodyWeight: Font.Normal
    // 시작 조립부가 초기화하며 모든 창이 공유하는 실제 글꼴. 독립 화면 검사도 같은 기본 규격을 쓴다.
    property font applicationFont: Qt.font({family: fontFamily, styleName: "", pixelSize: textBody, weight: bodyWeight})
    readonly property int pageSlideDuration: 180
    // 실제 크기는 네 단계뿐이며 화면에서는 아래 의미별 별칭을 선택한다.
    readonly property int typeBody: 14
    readonly property int typeEmphasis: 16
    readonly property int typeTitle: 20
    readonly property int typeDisplay: 28
    readonly property int textBody: typeBody
    readonly property int textCaption: typeBody
    readonly property int textCompact: typeBody
    readonly property int textAction: typeBody
    readonly property int textEmphasis: typeEmphasis
    readonly property int textWindowTitle: typeTitle
    readonly property int textSectionTitle: typeTitle
    readonly property int textAppTitle: typeDisplay
    readonly property int textPageTitle: typeDisplay
    readonly property int controlHeight: 40
    readonly property int numericInputWidth: 180
    readonly property int numericStepSize: 28
    readonly property int nodeAddMenuWidth: 224
    readonly property int ruleMenuWidth: 260
    readonly property int ruleHelpWidth: 440
    readonly property int ruleInspectorWidth: 280
    readonly property int ruleStatusHeight: 28
    readonly property int ruleSelectionHeaderHeight: 80
    readonly property int shortcutKeyWidth: 160
    readonly property int tooltipDelayMs: 500
    readonly property int primaryActionHeight: 46
    readonly property int largeActionHeight: 60
    readonly property int textDropHint: typeTitle
    readonly property real dropOverlayOpacity: 0.28
    readonly property int navigationHeight: 48
    readonly property int mainNavigationHeight: navigationHeight
    readonly property int homeShortcutLimit: 4
    readonly property int homeFooterDividerThickness: borderActive
    readonly property int homeFooterDividerSpacing: spaceControl
    readonly property int navigationIconSize: 24
    readonly property int textNavigation: typeEmphasis
    readonly property int mainMenuIconSize: navigationIconSize
    readonly property int homePortraitSize: 64
    readonly property int homeBadgeHeight: 22
    readonly property int textGroupTitle: typeEmphasis
    readonly property int navigationTextWeight: Font.DemiBold
    readonly property int groupTitleWeight: Font.DemiBold
    readonly property int settingsLabelWeight: Font.Medium
    readonly property int settingsSectionSpacing: 28
    readonly property int textSettingsGroup: typeBody
    readonly property int collisionChoiceMinHeight: 104
    readonly property int watchConditionInputWidth: 132
    readonly property int watchUnitWidth: 84
    readonly property int selectionFadeDuration: 140
    readonly property int buttonDefaultWidth: 100
    readonly property int buttonTransitionMs: 90
    readonly property int buttonReleaseMs: 120
    readonly property int confirmationContentWidth: 640
    readonly property int confirmationContentHeight: 480
    readonly property int dialogInset: 20
    readonly property int dialogCompactWidth: 420
    readonly property int dialogStandardWidth: 480
    readonly property int dialogWideWidth: 520
    readonly property int sortConfirmationWidth: 760
    readonly property int sortConfirmationHeight: 520
    readonly property int extensionHeaderHeight: 58
    readonly property int extensionHeaderRowHeight: 36
    readonly property int operationPopupWidth: 440
    readonly property int operationPopupHeight: 200
    readonly property real operationVeilOpacity: 0.72
    readonly property int historyListWidth: 300
    readonly property int historyRowHeight: 76
    readonly property int historySummaryHeight: 76
    readonly property int historyFooterHeight: 76
    readonly property int historyTypeTileSize: 36
    readonly property int windowButtonSize: 32
    readonly property int companionMenuWidth: 220
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
    readonly property int fileIconSize: 32
    readonly property int filePreviewSize: 48
    readonly property int fileRowHeight: 64
    readonly property int pendingRowHeight: 56
    readonly property int pendingPreviewSize: 36
    readonly property int pendingTypeWidth: 72
    readonly property int fileSortFooterHeight: 76
    readonly property int fileSortStepperHeight: 44
    readonly property int fileSortStepCircleSize: 22
    readonly property int fileSortStepConnectorWidth: 18
    readonly property int fileSortListHeaderHeight: 44
    readonly property int fileSortActionMinWidth: 140
    readonly property int dropHintMaxWidth: 480
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
    readonly property int windowResizeGrip: 7
    readonly property int homeDefaultHeight: 660
    // 페이지 순서는 Main의 페이지 번호와 같고 창 크기는 본체와 독립적이다.
    readonly property var extensionDefaults: [
        null,
        {width: 1100, height: 760, minWidth: 860, minHeight: 680},
        null, // 예전 독립 미리보기 페이지 번호는 재사용하지 않는다.
        {width: 720, height: 640, minWidth: 640, minHeight: 500},
        {width: 960, height: 700, minWidth: 860, minHeight: 540},
        {width: 1200, height: 820, minWidth: 1040, minHeight: 740},
        {width: 520, height: 360, minWidth: 460, minHeight: 280},
        {width: 720, height: 680, minWidth: 640, minHeight: 580},
        {width: 560, height: 520, minWidth: 500, minHeight: 520},
        {width: 960, height: 760, minWidth: 860, minHeight: 680},
        {width: 860, height: 700, minWidth: 720, minHeight: 540},
        {width: 560, height: 560, minWidth: 500, minHeight: 440},
        {width: 680, height: 760, minWidth: 540, minHeight: 520},
        {width: 640, height: 720, minWidth: 560, minHeight: 560},
        {width: 560, height: 640, minWidth: 500, minHeight: 440}
    ]

    // 규칙 캔버스의 글씨·블록 크기는 독립적인 표시 프리셋으로 관리한다.
    readonly property var nodeTextSizes: [typeBody, typeEmphasis, typeTitle]
    readonly property var nodeCaptionSizes: [typeBody, typeBody, typeEmphasis]
    readonly property var nodeSymbolSizes: [typeTitle, typeTitle, typeDisplay]
    readonly property var nodeWidths: [226, 262, 300]
    readonly property var nodeHeights: [90, 104, 120]
    readonly property int characterAnchorWidth: 640
    readonly property int characterAnchorHeight: 560
    readonly property int characterAnchorHandle: 22
    readonly property int characterAnchorHitSize: 32
    readonly property int characterAnchorVisualSize: 24
    readonly property int companionCoordinateWidth: 240
    readonly property int companionCoordinateEditorWidth: 360
    readonly property real companionPreviewZoomMin: 0.1
    readonly property real companionPreviewZoomMax: 4
    readonly property real companionPreviewZoomStep: 1.15
    readonly property int transientNoticeFadeMs: 160
    readonly property int transientNoticeHoldMs: 1400
    readonly property int companionSettingsWidth: 320
    readonly property int companionPackSelectorHeight: 64
    readonly property int companionPackPortraitSize: 40
    readonly property int companionPlaybackBadgeHeight: 28
    readonly property int companionPreviewGridSpacing: 20
    readonly property int switchWidth: 44
    readonly property int switchHeight: 26
    readonly property int switchThumbSize: 20
    readonly property int sliderTrackHeight: 4
    readonly property int sliderHandleSize: 20
    readonly property int companionTimelineHeight: 160
    readonly property int companionTimelineRulerHeight: 32
    readonly property int companionTimelineKeySize: 10
    readonly property int companionTimelineResizeHandleWidth: 12
    readonly property int companionTimelinePixelsPerSecond: 160
    readonly property int companionTimelineTickMs: 500
    readonly property int companionTimelineSnapMs: 100
    readonly property int companionFrameWidth: 84
    readonly property int companionFrameImageHeight: 48
    readonly property int bubbleTextSize: typeEmphasis
    readonly property int bubbleMaximumWidth: 320
    readonly property int bubbleMaximumLines: 4
    readonly property int bubblePadding: 14
    readonly property int bubbleScreenMargin: 8
}

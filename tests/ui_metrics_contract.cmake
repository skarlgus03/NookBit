# 화면에서 공통 글씨·여백·입력 부품을 다시 하드코딩하지 않는지 검사한다.
file(GLOB qml_files "${NOOKBIT_SOURCE_DIR}/src/presentation/qml/*.qml")
foreach(qml_file IN LISTS qml_files)
  get_filename_component(qml_name "${qml_file}" NAME)
  if(qml_name STREQUAL "UiMetrics.qml")
    continue()
  endif()
  file(READ "${qml_file}" source)
  # 페이지는 Main의 id나 앱 전역 Controller를 숨은 의존성으로 사용하지 않는다.
  if(qml_name MATCHES "^(AppSettingsPage|CollisionSettingsPage|CompanionPage|DeveloperToolsPage|HistoryPage|RecoveryPage|WatchSettingsPage|FileSortPage|RuleEditorPage|LegacyRuleEditorDialog|WatchProposalDialog|SortConfirmationDialog|OperationProgressPopup|GuideDownload)\\.qml$"
      AND source MATCHES "(root|extensionHost|previewController|pendingFiles|recoveryController|watchController|historyController|characterController|characterSettings|characterWindow|companionMotion|companionEdits|uiSettings|destinationSettings|backgroundTasks|userGuides|ruleEditor)\\.")
    message(FATAL_ERROR "${qml_name}: receive page dependencies through properties; request navigation through signals")
  endif()
  # 전문 편집기·캐릭터를 포함한 모든 QML은 공통 테마에서만 색상값을 정의한다.
  if(NOT qml_name STREQUAL "UiTheme.qml"
      AND source MATCHES "#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
    message(FATAL_ERROR "${qml_name}: use UiTheme roles instead of inline colors")
  endif()
  if(source MATCHES "font\\.pixelSize: *[0-9]+")
    message(FATAL_ERROR "${qml_name}: use a role from UiMetrics for font sizes")
  endif()
  if(source MATCHES "(spacing|padding|leftPadding|rightPadding|topPadding|bottomPadding): *[1-9][0-9]*")
    message(FATAL_ERROR "${qml_name}: use UiMetrics for shared spacing and padding")
  endif()
  if(NOT qml_name MATCHES "^App(Button|ComboBox|TextField|SpinBox)\\.qml$"
      AND source MATCHES "(^|[^A-Za-z.])(Button|ComboBox|TextField|SpinBox) +\\{")
    message(FATAL_ERROR "${qml_name}: use the common App control instead of a raw control")
  endif()
endforeach()

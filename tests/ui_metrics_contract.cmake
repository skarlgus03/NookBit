# 화면에서 공통 글씨·여백·입력 부품을 다시 하드코딩하지 않는지 검사한다.
file(GLOB qml_files "${NOOKBIT_SOURCE_DIR}/src/presentation/qml/*.qml")
foreach(qml_file IN LISTS qml_files)
  get_filename_component(qml_name "${qml_file}" NAME)
  if(qml_name STREQUAL "UiMetrics.qml")
    continue()
  endif()
  file(READ "${qml_file}" source)
  # 공통 설정·메뉴·강조 버튼은 테마 색상을 다시 직접 적지 않는다.
  if(qml_name MATCHES "^(SettingsSection|CompactHome|HomeMenuButton|LargeActionButton)\\.qml$"
      AND source MATCHES "#[0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f][0-9A-Fa-f]")
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

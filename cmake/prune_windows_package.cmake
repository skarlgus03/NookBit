# 개발용 Qt는 유지하고 생성된 Release 배포 폴더의 알려진 선택 부품만 제외한다.
get_filename_component(expected_package "${CMAKE_CURRENT_LIST_DIR}/../out/package/NookBit-Release" ABSOLUTE)
file(REAL_PATH "${NOOKBIT_PACKAGE_DIR}" package_root)
if(NOT package_root STREQUAL expected_package OR NOT EXISTS "${package_root}/nookbit.exe")
  message(FATAL_ERROR "Refusing to prune outside the generated NookBit Release package.")
endif()

# 대상의 실제 경로가 배포 폴더 내부임을 확인하고 재생성 가능한 Qt 부품을 제외하는 함수.
function(nookbit_exclude_runtime relative_path)
  set(candidate "${package_root}/${relative_path}")
  if(NOT EXISTS "${candidate}")
    return()
  endif()
  file(REAL_PATH "${candidate}" resolved)
  cmake_path(IS_PREFIX package_root "${resolved}" NORMALIZE within_package)
  if(NOT within_package OR resolved STREQUAL package_root OR IS_SYMLINK "${candidate}")
    message(FATAL_ERROR "Refusing to prune an unsafe runtime path: ${candidate}")
  endif()
  file(REMOVE_RECURSE "${candidate}")
  if(EXISTS "${candidate}")
    message(FATAL_ERROR "Cannot remove runtime component (close NookBit first): ${candidate}")
  endif()
endfunction()

# main.cpp에서 Basic을 고정한다. 스타일을 바꾸면 이 목록도 함께 수정해야 한다.
foreach(style IN ITEMS Fusion Imagine Material Universal FluentWinUI3 Windows)
  nookbit_exclude_runtime("qml/QtQuick/Controls/${style}")
  nookbit_exclude_runtime("Qt6QuickControls2${style}.dll")
  nookbit_exclude_runtime("Qt6QuickControls2${style}StyleImpl.dll")
  # Basic의 기본 대화상자는 유지하고 선택되지 않는 스타일별 대화상자만 제외한다.
  nookbit_exclude_runtime("qml/QtQuick/Dialogs/quickimpl/qml/+${style}")
endforeach()

# 이전 배포에서 남은 파일도 정리해 반복 패키징 후 다시 불어나지 않게 한다.
nookbit_exclude_runtime("qmltooling")
foreach(driver IN ITEMS qsqlibase qsqlmimer qsqloci qsqlodbc qsqlpsql)
  nookbit_exclude_runtime("sqldrivers/${driver}.dll")
endforeach()

# 이미지 로더·Qt Quick·그래픽 대체 경로·VC 실행 환경·캐릭터 팩은 보존한다.

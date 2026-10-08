# 배포 폴더에 실행 파일과 필요한 Qt 런타임만 모으는 스크립트.
if(NOT NOOKBIT_BUILD_CONFIG STREQUAL "Release")
  message(FATAL_ERROR "Use the release preset to package NookBit.")
endif()
if(NOT EXISTS "${NOOKBIT_EXECUTABLE}" OR NOT EXISTS "${NOOKBIT_DEPLOY_TOOL}")
  message(FATAL_ERROR "The Release executable or windeployqt is missing.")
endif()
file(MAKE_DIRECTORY "${NOOKBIT_PACKAGE_DIR}")
file(COPY_FILE "${NOOKBIT_EXECUTABLE}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe" ONLY_IF_DIFFERENT)
execute_process(
  COMMAND "${NOOKBIT_DEPLOY_TOOL}" --release --no-translations
    --include-plugins qoffscreen
    --qmldir "${NOOKBIT_QML_DIR}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe"
  RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "Qt runtime deployment failed: ${deploy_result}")
endif()
message(STATUS "NookBit package: ${NOOKBIT_PACKAGE_DIR}")

# 배포 폴더에 실행 파일과 필요한 Qt 런타임만 모으는 스크립트.
if(NOT NOOKBIT_BUILD_CONFIG STREQUAL "Release")
  message(FATAL_ERROR "Use the release preset to package NookBit.")
endif()
if(NOT EXISTS "${NOOKBIT_EXECUTABLE}" OR NOT EXISTS "${NOOKBIT_DEPLOY_TOOL}")
  message(FATAL_ERROR "The Release executable or windeployqt is missing.")
endif()
if(NOT EXISTS "${NOOKBIT_PACKAGE_INFO}/README.txt" OR NOT EXISTS "${NOOKBIT_PACKAGE_INFO}/version.json")
  message(FATAL_ERROR "Configured package instructions or version metadata are missing.")
endif()
get_filename_component(qt_bin "${NOOKBIT_DEPLOY_TOOL}" DIRECTORY)
execute_process(COMMAND "${NOOKBIT_DEPLOY_TOOL}" --version
  RESULT_VARIABLE qt_version_result OUTPUT_VARIABLE deploy_qt_version OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT qt_version_result EQUAL 0 OR NOT deploy_qt_version STREQUAL NOOKBIT_QT_VERSION)
  message(FATAL_ERROR "windeployqt must come from the Qt ${NOOKBIT_QT_VERSION} build kit.")
endif()
set(qt_notices "${CMAKE_CURRENT_LIST_DIR}/../resources/distribution/qt/${NOOKBIT_QT_VERSION}")
foreach(notice IN ITEMS LGPL-3.0.txt GPL-3.0.txt Qt-third-party.txt Mesa-11.2.2.txt LLVM-3.6.2.txt Qt-source-and-runtime.txt)
  if(NOT EXISTS "${qt_notices}/${notice}")
    message(FATAL_ERROR "Missing Windows distribution notice for Qt ${NOOKBIT_QT_VERSION}: ${notice}")
  endif()
endforeach()
file(MAKE_DIRECTORY "${NOOKBIT_PACKAGE_DIR}")
if(NOT EXISTS "${NOOKBIT_TEST_PACK}/imagepack.json" OR NOT EXISTS "${NOOKBIT_TEST_PACK}/images/test-character.png")
  message(FATAL_ERROR "The bundled test ImagePack is missing.")
endif()
file(COPY_FILE "${NOOKBIT_EXECUTABLE}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe" ONLY_IF_DIFFERENT)
file(COPY "${NOOKBIT_PACKAGE_INFO}/README.txt" "${NOOKBIT_PACKAGE_INFO}/version.json" DESTINATION "${NOOKBIT_PACKAGE_DIR}")
configure_file("${CMAKE_CURRENT_LIST_DIR}/../LICENSE" "${NOOKBIT_PACKAGE_DIR}/LICENSE" COPYONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/../resources/fonts/OFL.txt"
  "${NOOKBIT_PACKAGE_DIR}/licenses/NotoSansKR-OFL.txt" COPYONLY)
file(COPY "${NOOKBIT_TEST_PACK}/" DESTINATION "${NOOKBIT_PACKAGE_DIR}/imagepacks/test")
if(NOT EXISTS "${NOOKBIT_NMOO_PACK}/imagepack.json")
  message(FATAL_ERROR "The bundled Nmoo ImagePack is missing.")
endif()
file(COPY "${NOOKBIT_NMOO_PACK}/" DESTINATION "${NOOKBIT_PACKAGE_DIR}/imagepacks/nmoo")
set(deploy_options)
if(NOOKBIT_COMPACT_PACKAGE)
  list(APPEND deploy_options --skip-plugin-types qmltooling
    --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql)
endif()
execute_process(
  COMMAND "${NOOKBIT_DEPLOY_TOOL}" --release --no-translations --no-patchqt
    --no-quick3dutils --no-system-dxc-compiler
    ${deploy_options}
    --include-plugins qoffscreen
    --qmldir "${NOOKBIT_QML_DIR}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe"
  RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "Qt runtime deployment failed: ${deploy_result}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/prune_windows_package.cmake")
# 이전 패키지에 남은 수정된 QtCore도 SDK 원본으로 교체한다.
file(COPY_FILE "${qt_bin}/Qt6Core.dll" "${NOOKBIT_PACKAGE_DIR}/Qt6Core.dll" ONLY_IF_DIFFERENT)
file(COPY "${qt_notices}/" DESTINATION "${NOOKBIT_PACKAGE_DIR}/licenses/qt")
set(qt_sboms qtbase qtdeclarative qtsvg)
if(EXISTS "${NOOKBIT_PACKAGE_DIR}/Qt6Pdf.dll")
  list(APPEND qt_sboms qtpdf)
endif()
foreach(module IN LISTS qt_sboms)
  configure_file("${qt_bin}/../sbom/${module}-${NOOKBIT_QT_VERSION}.spdx.json"
    "${NOOKBIT_PACKAGE_DIR}/licenses/qt/${module}.spdx.json" COPYONLY)
endforeach()
message(STATUS "NookBit package: ${NOOKBIT_PACKAGE_DIR}")

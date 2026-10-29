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
file(MAKE_DIRECTORY "${NOOKBIT_PACKAGE_DIR}")
if(NOT EXISTS "${NOOKBIT_TEST_PACK}/imagepack.json" OR NOT EXISTS "${NOOKBIT_TEST_PACK}/images/test-character.png")
  message(FATAL_ERROR "The bundled test ImagePack is missing.")
endif()
file(COPY_FILE "${NOOKBIT_EXECUTABLE}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe" ONLY_IF_DIFFERENT)
file(COPY "${NOOKBIT_PACKAGE_INFO}/README.txt" "${NOOKBIT_PACKAGE_INFO}/version.json" DESTINATION "${NOOKBIT_PACKAGE_DIR}")
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
  COMMAND "${NOOKBIT_DEPLOY_TOOL}" --release --no-translations
    ${deploy_options}
    --include-plugins qoffscreen
    --qmldir "${NOOKBIT_QML_DIR}" "${NOOKBIT_PACKAGE_DIR}/nookbit.exe"
  RESULT_VARIABLE deploy_result)
if(NOT deploy_result EQUAL 0)
  message(FATAL_ERROR "Qt runtime deployment failed: ${deploy_result}")
endif()
if(NOOKBIT_COMPACT_PACKAGE)
  include("${CMAKE_CURRENT_LIST_DIR}/prune_windows_package.cmake")
endif()
message(STATUS "NookBit package: ${NOOKBIT_PACKAGE_DIR}")

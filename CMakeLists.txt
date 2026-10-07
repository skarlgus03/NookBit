cmake_minimum_required(VERSION 3.24)

project(
  NookBit
  VERSION 0.1.0
  DESCRIPTION "Safety-first desktop file organizer"
  LANGUAGES CXX
)

option(NOOKBIT_BUILD_TESTS "Build core tests" ON)
option(NOOKBIT_BUILD_QT_APP "Build the Qt/QML desktop shell when Qt is available" ON)
set(NOOKBIT_DEV_DESTINATION_ROOT "" CACHE PATH "Optional fixed destination for this local development build")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 한국어 /showIncludes의 인코딩 불일치로 Ninja가 헤더 의존성을 놓치지 않게 고정한다.
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR MATCHES "Ninja")
  set(CMAKE_CL_SHOWINCLUDES_PREFIX "Note: including file: ")
  set(CMAKE_CXX_COMPILER_LAUNCHER
    "powershell.exe" "-NoProfile" "-ExecutionPolicy" "Bypass" "-File"
    "${CMAKE_SOURCE_DIR}/cmake/msvc_showincludes.ps1" ${CMAKE_CXX_COMPILER_LAUNCHER})
endif()

add_subdirectory(src)

if(NOOKBIT_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
  if(TARGET nookbit AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC" AND CMAKE_GENERATOR MATCHES "Ninja")
    add_test(NAME nookbit_header_dependency_regression
      COMMAND "${CMAKE_COMMAND}"
        "-DNOOKBIT_NINJA=${CMAKE_MAKE_PROGRAM}" "-DNOOKBIT_BUILD_DIR=${CMAKE_BINARY_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/verify_msvc_header_dependencies.cmake")
  endif()
endif()

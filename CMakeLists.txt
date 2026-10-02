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

add_subdirectory(src)

if(NOOKBIT_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()

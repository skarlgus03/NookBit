add_library(nookbit_core STATIC
  app/composition_root.cpp
  application/use_cases/create_preview_use_case.cpp
  application/use_cases/content_fingerprint.cpp
  application/use_cases/approve_plan_use_case.cpp
  application/use_cases/freeze_preview_use_case.cpp
  application/use_cases/plan_signature.cpp
  application/use_cases/verify_saved_plan_use_case.cpp
  application/use_cases/evaluate_watch_folder_use_case.cpp
  modules/rules/rule_engine.cpp
  modules/watch/watch_evaluator.cpp
  modules/planner/collision_resolver.cpp
  modules/planner/planner.cpp
  modules/planner/path_safety_validator.cpp
)

target_include_directories(nookbit_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(nookbit_core PUBLIC cxx_std_20)

add_library(nookbit_filesystem STATIC
  infrastructure/filesystem/filesystem_scanner.cpp
  infrastructure/filesystem/file_identity.cpp
  infrastructure/filesystem/operation_mutex.cpp
  infrastructure/filesystem/recovery_service.cpp
  infrastructure/filesystem/windows_file_mover.cpp
  infrastructure/filesystem/safe_executor.cpp
)
target_include_directories(nookbit_filesystem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nookbit_filesystem PUBLIC nookbit_core)
target_compile_features(nookbit_filesystem PUBLIC cxx_std_20)

if(MSVC)
  target_compile_options(nookbit_core PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(nookbit_filesystem PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(nookbit_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  target_compile_options(nookbit_filesystem PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(NOOKBIT_BUILD_QT_APP)
  find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Qml Quick Sql)

  if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    add_library(nookbit_json_config STATIC
      infrastructure/configuration/json_document_loader.cpp
      infrastructure/configuration/json_settings_repository.cpp
      infrastructure/configuration/json_watch_settings_repository.cpp
      infrastructure/configuration/json_rule_set_repository.cpp
    )
    target_include_directories(nookbit_json_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nookbit_json_config PUBLIC nookbit_core Qt6::Core)

    add_library(nookbit_history STATIC
      infrastructure/cryptography/qt_plan_hasher.cpp
      infrastructure/persistence_sqlite/sqlite_history_repository.cpp
    )
    target_include_directories(nookbit_history PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nookbit_history PUBLIC nookbit_core Qt6::Core Qt6::Sql)

    qt_add_executable(nookbit
      presentation/main.cpp
      presentation/pending_files_model.hpp
      presentation/pending_files_model.cpp
      presentation/preview_controller.hpp
      presentation/preview_controller.cpp
      presentation/history_controller.hpp
      presentation/history_controller.cpp
      presentation/recovery_controller.hpp
      presentation/recovery_controller.cpp
      presentation/watch_controller.hpp
      presentation/watch_controller.cpp
      presentation/rule_editor_controller.hpp
      presentation/rule_editor_controller.cpp
      ${PROJECT_SOURCE_DIR}/resources/config.qrc
    )
    set_target_properties(nookbit PROPERTIES AUTORCC ON)
    target_include_directories(nookbit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    qt_add_qml_module(nookbit
      URI NookBit
      VERSION 1.0
      QML_FILES
        presentation/qml/Main.qml
    )

    target_link_libraries(nookbit PRIVATE
      nookbit_core
      nookbit_filesystem
      nookbit_json_config
      nookbit_history
      Qt6::Core
      Qt6::Gui
      Qt6::Qml
      Qt6::Quick
      Qt6::Sql
    )
  else()
    message(STATUS "Qt 6.5+ not found: building the tested C++ core only")
  endif()
endif()

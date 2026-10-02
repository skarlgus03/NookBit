add_executable(nookbit_core_tests
  core_tests.cpp
)

add_executable(nookbit_preview_use_case_tests
  create_preview_use_case_tests.cpp
)

add_executable(nookbit_rule_engine_tests
  rule_engine_tests.cpp
)

add_executable(nookbit_filesystem_scanner_tests
  filesystem_scanner_tests.cpp
)
target_link_libraries(nookbit_filesystem_scanner_tests PRIVATE nookbit_filesystem)

add_executable(nookbit_planner_tests
  planner_tests.cpp
)
target_link_libraries(nookbit_planner_tests PRIVATE nookbit_filesystem)

add_executable(nookbit_watch_evaluator_tests watch_evaluator_tests.cpp)

set(NOOKBIT_TEST_TARGETS
  nookbit_core_tests
  nookbit_preview_use_case_tests
  nookbit_rule_engine_tests
  nookbit_filesystem_scanner_tests
  nookbit_planner_tests
  nookbit_watch_evaluator_tests
)

foreach(target IN LISTS NOOKBIT_TEST_TARGETS)
  target_link_libraries(${target} PRIVATE nookbit_core)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(${target} PRIVATE cxx_std_20)

  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
endforeach()

add_test(NAME nookbit_core_tests COMMAND nookbit_core_tests)
add_test(NAME nookbit_preview_use_case_tests COMMAND nookbit_preview_use_case_tests)
add_test(NAME nookbit_rule_engine_tests COMMAND nookbit_rule_engine_tests)
add_test(NAME nookbit_filesystem_scanner_tests COMMAND nookbit_filesystem_scanner_tests)
add_test(NAME nookbit_planner_tests COMMAND nookbit_planner_tests)
add_test(NAME nookbit_watch_evaluator_tests COMMAND nookbit_watch_evaluator_tests)

if(TARGET nookbit_json_config)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql)
  add_executable(nookbit_json_config_tests json_config_tests.cpp)
  target_link_libraries(nookbit_json_config_tests PRIVATE nookbit_json_config)
  target_include_directories(nookbit_json_config_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_json_config_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(nookbit_json_config_tests PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(nookbit_json_config_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  add_test(NAME nookbit_json_config_tests COMMAND nookbit_json_config_tests)
  add_executable(nookbit_rule_editor_controller_tests
    rule_editor_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.cpp
  )
  set_target_properties(nookbit_rule_editor_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_rule_editor_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_rule_editor_controller_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_rule_editor_controller_tests PRIVATE nookbit_filesystem nookbit_json_config Qt6::Core)
  add_test(NAME nookbit_rule_editor_controller_tests COMMAND nookbit_rule_editor_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_rule_editor_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  if(WIN32)
    set_tests_properties(nookbit_json_config_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_pending_files_model_tests
    pending_files_model_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
  )
  set_target_properties(nookbit_pending_files_model_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_pending_files_model_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_pending_files_model_tests PRIVATE Qt6::Core)
  add_test(NAME nookbit_pending_files_model_tests COMMAND nookbit_pending_files_model_tests)
  if(WIN32)
    set_tests_properties(nookbit_pending_files_model_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_watch_controller_tests
    watch_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.cpp
  )
  set_target_properties(nookbit_watch_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_watch_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_watch_controller_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_watch_controller_tests PRIVATE
    nookbit_filesystem nookbit_json_config Qt6::Core)
  add_test(NAME nookbit_watch_controller_tests COMMAND nookbit_watch_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_watch_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_preview_controller_tests
    preview_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/recovery_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/recovery_controller.cpp
  )
  set_target_properties(nookbit_preview_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_preview_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_preview_controller_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_preview_controller_tests PRIVATE
    nookbit_filesystem nookbit_json_config nookbit_history Qt6::Core Qt6::Sql)
  add_test(NAME nookbit_preview_controller_tests COMMAND nookbit_preview_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_preview_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_safe_executor_tests safe_executor_tests.cpp)
  target_include_directories(nookbit_safe_executor_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_safe_executor_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_safe_executor_tests PRIVATE
    nookbit_filesystem nookbit_history nookbit_json_config Qt6::Core Qt6::Sql)
  add_test(NAME nookbit_safe_executor_tests COMMAND nookbit_safe_executor_tests)
  if(WIN32)
    set_tests_properties(nookbit_safe_executor_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_recovery_service_tests recovery_service_tests.cpp)
  target_include_directories(nookbit_recovery_service_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_recovery_service_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_recovery_service_tests PRIVATE
    nookbit_filesystem nookbit_history nookbit_json_config Qt6::Core Qt6::Sql)
  add_test(NAME nookbit_recovery_service_tests COMMAND nookbit_recovery_service_tests)
  if(WIN32)
    set_tests_properties(nookbit_recovery_service_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_test(NAME nookbit_qml_smoke COMMAND nookbit --verify-ui)
  if(WIN32)
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_QUICK_BACKEND=set:software")
  else()
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()
endif()

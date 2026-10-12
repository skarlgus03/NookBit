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
add_executable(nookbit_interrupted_plan_tests interrupted_plan_tests.cpp)
add_executable(nookbit_operation_timing_tests operation_timing_tests.cpp)
add_executable(nookbit_operation_progress_tests operation_progress_tests.cpp)
target_link_libraries(nookbit_operation_timing_tests PRIVATE nookbit_filesystem)

set(NOOKBIT_TEST_TARGETS
  nookbit_core_tests
  nookbit_preview_use_case_tests
  nookbit_rule_engine_tests
  nookbit_filesystem_scanner_tests
  nookbit_planner_tests
  nookbit_watch_evaluator_tests
  nookbit_interrupted_plan_tests
  nookbit_operation_timing_tests
  nookbit_operation_progress_tests
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
add_test(NAME nookbit_interrupted_plan_tests COMMAND nookbit_interrupted_plan_tests)
add_test(NAME nookbit_operation_timing_tests COMMAND nookbit_operation_timing_tests)
add_test(NAME nookbit_operation_progress_tests COMMAND nookbit_operation_progress_tests)

if(TARGET nookbit_json_config)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Gui Qml Quick)
  add_executable(nookbit_plan_contract_tests plan_contract_tests.cpp)
  target_link_libraries(nookbit_plan_contract_tests PRIVATE nookbit_history Qt6::Core Qt6::Sql)
  target_include_directories(nookbit_plan_contract_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_plan_contract_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(nookbit_plan_contract_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  add_test(NAME nookbit_plan_contract_tests COMMAND nookbit_plan_contract_tests)
  if(WIN32)
    set_tests_properties(nookbit_plan_contract_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  target_link_libraries(nookbit_pending_files_model_tests PRIVATE nookbit_json_config Qt6::Core)
  add_test(NAME nookbit_pending_files_model_tests COMMAND nookbit_pending_files_model_tests)
  if(WIN32)
    set_tests_properties(nookbit_pending_files_model_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_image_pack_loader_tests image_pack_loader_tests.cpp)
  add_executable(nookbit_character_window_tests character_window_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_window_controller.hpp)
  add_executable(nookbit_character_bubble_tests character_bubble_tests.cpp)
  target_compile_definitions(nookbit_character_bubble_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_character_bubble_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  add_test(NAME nookbit_character_bubble_tests COMMAND nookbit_character_bubble_tests)
  set_tests_properties(nookbit_character_bubble_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  if(WIN32)
    set_tests_properties(nookbit_character_bubble_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_app_lifecycle_tests app_lifecycle_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/app_lifecycle_controller.hpp)
  add_executable(nookbit_ui_settings_tests ui_settings_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/ui_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/destination_settings_controller.hpp)
  set_target_properties(nookbit_ui_settings_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_ui_settings_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_ui_settings_tests PRIVATE Qt6::Core)
  add_test(NAME nookbit_ui_settings_tests COMMAND nookbit_ui_settings_tests)
  if(WIN32)
    set_tests_properties(nookbit_ui_settings_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  set_target_properties(nookbit_app_lifecycle_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_app_lifecycle_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_app_lifecycle_tests PRIVATE Qt6::Core)
  add_test(NAME nookbit_app_lifecycle_tests COMMAND nookbit_app_lifecycle_tests)
  if(WIN32)
    set_tests_properties(nookbit_app_lifecycle_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  set_target_properties(nookbit_character_window_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_character_window_tests PRIVATE Qt6::Core)
  add_test(NAME nookbit_character_window_tests COMMAND nookbit_character_window_tests)
  if(WIN32)
    set_tests_properties(nookbit_character_window_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_image_pack_library_tests
    image_pack_library_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp)
  set_target_properties(nookbit_image_pack_library_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_image_pack_library_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_image_pack_library_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_image_pack_library_tests PRIVATE nookbit_json_config Qt6::Gui)
  add_test(NAME nookbit_image_pack_library_tests COMMAND nookbit_image_pack_library_tests)
  if(WIN32)
    set_tests_properties(nookbit_image_pack_library_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  target_include_directories(nookbit_image_pack_loader_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_image_pack_loader_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_image_pack_loader_tests PRIVATE nookbit_json_config Qt6::Gui)
  add_test(NAME nookbit_image_pack_loader_tests COMMAND nookbit_image_pack_loader_tests)
  if(WIN32)
    set_tests_properties(nookbit_image_pack_loader_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_character_controller_tests
    character_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp
  )
  set_target_properties(nookbit_character_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_character_controller_tests PRIVATE nookbit_json_config Qt6::Core)
  target_compile_definitions(nookbit_character_controller_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  add_test(NAME nookbit_character_controller_tests COMMAND nookbit_character_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_character_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()

  add_executable(nookbit_character_sprite_tests
    character_sprite_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp
  )
  set_target_properties(nookbit_character_sprite_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_sprite_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_character_sprite_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_character_sprite_tests PRIVATE nookbit_json_config Qt6::Gui Qt6::Qml Qt6::Quick)
  add_test(NAME nookbit_character_sprite_tests COMMAND nookbit_character_sprite_tests)
  set_tests_properties(nookbit_character_sprite_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  if(WIN32)
    set_tests_properties(nookbit_character_sprite_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
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
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp
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
  add_executable(nookbit_staged_copy_tests staged_copy_tests.cpp)
  add_executable(nookbit_commit_transfer_copy_tests commit_transfer_copy_tests.cpp)
  if(WIN32)
    # 다른 드라이브를 변경하는 시험은 CTest에 등록하지 않고 명시적으로만 실행한다.
    add_executable(nookbit_cross_volume_tests cross_volume_tests.cpp)
    target_include_directories(nookbit_cross_volume_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_compile_definitions(nookbit_cross_volume_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(nookbit_cross_volume_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core Qt6::Sql)
    if(MSVC)
      target_compile_options(nookbit_cross_volume_tests PRIVATE /W4 /permissive- /utf-8)
    endif()
  endif()
  target_include_directories(nookbit_commit_transfer_copy_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_commit_transfer_copy_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_commit_transfer_copy_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core Qt6::Sql)
  add_test(NAME nookbit_commit_transfer_copy_tests COMMAND nookbit_commit_transfer_copy_tests)
  if(MSVC)
    target_compile_options(nookbit_commit_transfer_copy_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(WIN32)
    set_tests_properties(nookbit_commit_transfer_copy_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  target_include_directories(nookbit_staged_copy_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_staged_copy_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core)
  add_test(NAME nookbit_staged_copy_tests COMMAND nookbit_staged_copy_tests)
  if(MSVC)
    target_compile_options(nookbit_staged_copy_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(WIN32)
    set_tests_properties(nookbit_staged_copy_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_transfer_journal_tests transfer_journal_tests.cpp)
  target_include_directories(nookbit_transfer_journal_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_transfer_journal_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_transfer_journal_tests PRIVATE nookbit_history Qt6::Core Qt6::Sql)
  add_test(NAME nookbit_transfer_journal_tests COMMAND nookbit_transfer_journal_tests)
  if(MSVC)
    target_compile_options(nookbit_transfer_journal_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  if(WIN32)
    set_tests_properties(nookbit_transfer_journal_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_ui_metrics_contract COMMAND ${CMAKE_COMMAND}
    "-DNOOKBIT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ui_metrics_contract.cmake")
  if(WIN32)
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_QUICK_BACKEND=set:software")
  else()
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()
endif()

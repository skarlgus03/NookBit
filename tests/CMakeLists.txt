# Register a core test with the common compiler settings.
function(nookbit_add_core_test target source)
  add_executable(${target} ${source})
  target_link_libraries(${target} PRIVATE ${ARGN} nookbit_core)
  target_include_directories(${target} PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(${target} PRIVATE cxx_std_20)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  endif()
  add_test(NAME ${target} COMMAND ${target})
endfunction()

nookbit_add_core_test(nookbit_core_tests core_tests.cpp)
nookbit_add_core_test(nookbit_preview_use_case_tests create_preview_use_case_tests.cpp)
nookbit_add_core_test(nookbit_rule_engine_tests rule_engine_tests.cpp)
nookbit_add_core_test(nookbit_filesystem_scanner_tests filesystem_scanner_tests.cpp nookbit_filesystem)
nookbit_add_core_test(nookbit_planner_tests planner_tests.cpp nookbit_filesystem)
nookbit_add_core_test(nookbit_watch_evaluator_tests watch_evaluator_tests.cpp)
nookbit_add_core_test(nookbit_classification_query_tests classification_query_tests.cpp)
nookbit_add_core_test(nookbit_notification_center_tests notification_center_tests.cpp)
nookbit_add_core_test(nookbit_companion_runtime_tests companion_runtime_tests.cpp)
nookbit_add_core_test(nookbit_interrupted_plan_tests interrupted_plan_tests.cpp)
nookbit_add_core_test(nookbit_operation_timing_tests operation_timing_tests.cpp nookbit_filesystem)
nookbit_add_core_test(nookbit_operation_progress_tests operation_progress_tests.cpp)

if(TARGET nookbit_json_config)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Gui Widgets Qml Quick QuickControls2)

  # Give Qt tests access to the SDK DLLs and, when needed, the platform plugins.
  function(nookbit_add_qt_test name)
    cmake_parse_arguments(PARSE_ARGV 1 test "PLATFORM_PLUGINS" "" "COMMAND")
    if(NOT test_COMMAND)
      set(test_COMMAND ${name})
    endif()
    add_test(NAME ${name} COMMAND ${test_COMMAND})
    if(WIN32)
      set(runtime_environment "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
      if(test_PLATFORM_PLUGINS)
        list(APPEND runtime_environment "QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
      endif()
      set_tests_properties(${name} PROPERTIES ENVIRONMENT_MODIFICATION "${runtime_environment}")
    endif()
  endfunction()

  add_executable(nookbit_companion_tray_tests companion_tray_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_tray_action.hpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_tray_action.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_window_controller.hpp)
  set_target_properties(nookbit_companion_tray_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_companion_tray_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(nookbit_companion_tray_tests PRIVATE cxx_std_20)
  target_link_libraries(nookbit_companion_tray_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  if(MSVC)
    target_compile_options(nookbit_companion_tray_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_companion_tray_tests)
  set_tests_properties(nookbit_companion_tray_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(nookbit_ui_page_registry_tests ui_page_registry_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.hpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.cpp)
  set_target_properties(nookbit_ui_page_registry_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_ui_page_registry_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(nookbit_ui_page_registry_tests PRIVATE cxx_std_20)
  target_link_libraries(nookbit_ui_page_registry_tests PRIVATE nookbit_core Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_ui_page_registry_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_ui_page_registry_tests)
  add_executable(nookbit_owned_runtime_tests
    owned_runtime_tests.cpp
    ${PROJECT_SOURCE_DIR}/resources/config.qrc
    ${PROJECT_SOURCE_DIR}/src/app/file_organizer_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_tray_action.hpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_tray_action.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.hpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/preview_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/recovery_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/recovery_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/rule_editor_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/destination_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_window_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_edit_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_edit_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_motion_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_motion_controller.cpp)
  set_target_properties(nookbit_owned_runtime_tests PROPERTIES AUTOMOC ON AUTORCC ON)
  target_include_directories(nookbit_owned_runtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_owned_runtime_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_compile_features(nookbit_owned_runtime_tests PRIVATE cxx_std_20)
  target_link_libraries(nookbit_owned_runtime_tests PRIVATE nookbit_filesystem nookbit_json_config nookbit_history nookbit_document_recommendation Qt6::Core Qt6::Sql Qt6::Gui Qt6::Widgets)
  if(MSVC)
    target_compile_options(nookbit_owned_runtime_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_owned_runtime_tests)
  set_tests_properties(nookbit_owned_runtime_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
  add_executable(nookbit_feature_runtime_tests
    feature_runtime_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.hpp
    ${PROJECT_SOURCE_DIR}/src/app/ui_page_registry.cpp
    ${PROJECT_SOURCE_DIR}/src/app/notification_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/app/folder_watch_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.cpp)
  set_target_properties(nookbit_feature_runtime_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_feature_runtime_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_feature_runtime_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_compile_features(nookbit_feature_runtime_tests PRIVATE cxx_std_20)
  target_link_libraries(nookbit_feature_runtime_tests PRIVATE nookbit_filesystem nookbit_json_config Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_feature_runtime_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_feature_runtime_tests)
  add_executable(nookbit_feature_connections_tests
    feature_connections_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/feature_connections.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/watch_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp)
  set_target_properties(nookbit_feature_connections_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_feature_connections_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_features(nookbit_feature_connections_tests PRIVATE cxx_std_20)
  target_link_libraries(nookbit_feature_connections_tests PRIVATE nookbit_core Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_feature_connections_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_feature_connections_tests)
  add_executable(nookbit_directory_bundle_tests directory_bundle_tests.cpp)
  add_executable(nookbit_folder_flow_tests folder_flow_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp)
  set_target_properties(nookbit_folder_flow_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_folder_flow_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_folder_flow_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_folder_flow_tests PRIVATE nookbit_filesystem nookbit_history nookbit_json_config Qt6::Core Qt6::Sql)
  if(MSVC)
    target_compile_options(nookbit_folder_flow_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_folder_flow_tests)
  target_include_directories(nookbit_directory_bundle_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_directory_bundle_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_directory_bundle_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core Qt6::Sql)
  if(MSVC)
    target_compile_options(nookbit_directory_bundle_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_directory_bundle_tests)
  add_executable(nookbit_companion_motion_tests companion_motion_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_motion_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_motion_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp)
  set_target_properties(nookbit_companion_motion_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_companion_motion_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_companion_motion_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_companion_motion_tests PRIVATE nookbit_json_config Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_companion_motion_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_companion_motion_tests)
  add_executable(nookbit_companion_interaction_controller_tests companion_interaction_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp)
  set_target_properties(nookbit_companion_interaction_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_companion_interaction_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_companion_interaction_controller_tests PRIVATE nookbit_core Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_companion_interaction_controller_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_companion_interaction_controller_tests)
  add_executable(nookbit_notification_controller_tests notification_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.cpp)
  set_target_properties(nookbit_notification_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_notification_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_notification_controller_tests PRIVATE nookbit_core Qt6::Core)
  nookbit_add_qt_test(nookbit_notification_controller_tests)
  add_executable(nookbit_plan_contract_tests plan_contract_tests.cpp)
  target_link_libraries(nookbit_plan_contract_tests PRIVATE nookbit_history Qt6::Core Qt6::Sql)
  target_include_directories(nookbit_plan_contract_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_plan_contract_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  if(MSVC)
    target_compile_options(nookbit_plan_contract_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_plan_contract_tests)
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
  nookbit_add_qt_test(nookbit_json_config_tests)
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
  nookbit_add_qt_test(nookbit_rule_editor_controller_tests)

  add_executable(nookbit_pending_files_model_tests
    pending_files_model_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/pending_files_model.cpp
  )
  set_target_properties(nookbit_pending_files_model_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_pending_files_model_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_pending_files_model_tests PRIVATE nookbit_json_config Qt6::Core)
  nookbit_add_qt_test(nookbit_pending_files_model_tests)

  add_executable(nookbit_image_pack_loader_tests image_pack_loader_tests.cpp)
  add_executable(nookbit_character_window_tests character_window_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_window_controller.hpp)
  add_executable(nookbit_character_bubble_tests character_bubble_tests.cpp)
  target_compile_definitions(nookbit_character_bubble_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_character_bubble_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  nookbit_add_qt_test(nookbit_character_bubble_tests)
  set_tests_properties(nookbit_character_bubble_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  add_executable(nookbit_app_lifecycle_tests app_lifecycle_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/app_lifecycle_controller.hpp)
  add_executable(nookbit_ui_settings_tests ui_settings_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/ui_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/destination_settings_controller.hpp)
  add_executable(nookbit_user_guide_tests user_guide_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/user_guide_controller.hpp
    ${PROJECT_SOURCE_DIR}/resources/config.qrc)
  set_target_properties(nookbit_user_guide_tests PROPERTIES AUTOMOC ON AUTORCC ON)
  target_include_directories(nookbit_user_guide_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_user_guide_tests PRIVATE nookbit_json_config Qt6::Gui)
  nookbit_add_qt_test(nookbit_user_guide_tests)
  set_target_properties(nookbit_ui_settings_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_ui_settings_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_ui_settings_tests PRIVATE Qt6::Core)
  nookbit_add_qt_test(nookbit_ui_settings_tests)
  set_target_properties(nookbit_app_lifecycle_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_app_lifecycle_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_app_lifecycle_tests PRIVATE Qt6::Core)
  nookbit_add_qt_test(nookbit_app_lifecycle_tests)
  set_target_properties(nookbit_character_window_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_window_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_character_window_tests PRIVATE Qt6::Core)
  nookbit_add_qt_test(nookbit_character_window_tests)
  add_executable(nookbit_image_pack_library_tests
    image_pack_library_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_settings_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp)
  set_target_properties(nookbit_image_pack_library_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_image_pack_library_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_image_pack_library_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_image_pack_library_tests PRIVATE nookbit_json_config Qt6::Gui)
  nookbit_add_qt_test(nookbit_image_pack_library_tests)
  target_include_directories(nookbit_image_pack_loader_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_image_pack_loader_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_image_pack_loader_tests PRIVATE nookbit_json_config Qt6::Gui)
  nookbit_add_qt_test(nookbit_image_pack_loader_tests)

  add_executable(nookbit_character_controller_tests
    character_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_edit_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_edit_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp
  )
  set_target_properties(nookbit_character_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_character_controller_tests PRIVATE nookbit_json_config Qt6::Core)
  target_compile_definitions(nookbit_character_controller_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  nookbit_add_qt_test(nookbit_character_controller_tests)

  add_executable(nookbit_character_sprite_tests
    character_sprite_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp
  )
  set_target_properties(nookbit_character_sprite_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_character_sprite_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_character_sprite_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_character_sprite_tests PRIVATE nookbit_json_config Qt6::Gui Qt6::Qml Qt6::Quick)
  nookbit_add_qt_test(nookbit_character_sprite_tests PLATFORM_PLUGINS)
  set_tests_properties(nookbit_character_sprite_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")

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
    nookbit_filesystem nookbit_json_config nookbit_history Qt6::Core)
  nookbit_add_qt_test(nookbit_watch_controller_tests)

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
    nookbit_filesystem nookbit_json_config nookbit_history nookbit_document_recommendation Qt6::Core Qt6::Sql)
  nookbit_add_qt_test(nookbit_preview_controller_tests)

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
  nookbit_add_qt_test(nookbit_commit_transfer_copy_tests)
  if(MSVC)
    target_compile_options(nookbit_commit_transfer_copy_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  target_include_directories(nookbit_staged_copy_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_staged_copy_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core)
  nookbit_add_qt_test(nookbit_staged_copy_tests)
  if(MSVC)
    target_compile_options(nookbit_staged_copy_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  add_executable(nookbit_transfer_journal_tests transfer_journal_tests.cpp)
  target_include_directories(nookbit_transfer_journal_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_transfer_journal_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_transfer_journal_tests PRIVATE nookbit_history Qt6::Core Qt6::Sql)
  nookbit_add_qt_test(nookbit_transfer_journal_tests)
  if(MSVC)
    target_compile_options(nookbit_transfer_journal_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  target_include_directories(nookbit_safe_executor_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_safe_executor_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_safe_executor_tests PRIVATE
    nookbit_filesystem nookbit_history nookbit_json_config Qt6::Core Qt6::Sql)
  nookbit_add_qt_test(nookbit_safe_executor_tests)

  add_executable(nookbit_recovery_service_tests recovery_service_tests.cpp)
  target_include_directories(nookbit_recovery_service_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_recovery_service_tests PRIVATE
    NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_recovery_service_tests PRIVATE
    nookbit_filesystem nookbit_history nookbit_json_config Qt6::Core Qt6::Sql)
  nookbit_add_qt_test(nookbit_recovery_service_tests)

  add_test(NAME nookbit_qml_smoke COMMAND nookbit_ui_checks --verify-ui)
  nookbit_add_qt_test(nookbit_tutorial_smoke PLATFORM_PLUGINS COMMAND nookbit_ui_checks --verify-tutorial)
  nookbit_add_qt_test(nookbit_tutorial_skip_smoke PLATFORM_PLUGINS COMMAND nookbit_ui_checks --verify-tutorial-skip)
  set_tests_properties(nookbit_tutorial_smoke nookbit_tutorial_skip_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 45)
  add_executable(nookbit_tutorial_tests tutorial_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/tutorial_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/tutorial_controller.cpp)
  set_target_properties(nookbit_tutorial_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_tutorial_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_tutorial_tests PRIVATE nookbit_core Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_tutorial_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_tutorial_tests)
  add_executable(nookbit_ui_font_tests ui_font_tests.cpp ${PROJECT_SOURCE_DIR}/resources/fonts.qrc)
  set_target_properties(nookbit_ui_font_tests PROPERTIES AUTORCC ON)
  target_include_directories(nookbit_ui_font_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_ui_font_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_ui_font_tests PRIVATE Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2)
  if(MSVC)
    target_compile_options(nookbit_ui_font_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_ui_font_tests PLATFORM_PLUGINS)
  set_tests_properties(nookbit_ui_font_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  add_executable(nookbit_feeding_interaction_tests feeding_interaction_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/companion_feeding_runtime.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/companion_interaction_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/character_controller.cpp)
  set_target_properties(nookbit_feeding_interaction_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_feeding_interaction_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_feeding_interaction_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_feeding_interaction_tests PRIVATE nookbit_json_config Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_feeding_interaction_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_feeding_interaction_tests)
  add_executable(nookbit_history_search_tests history_search_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/history_controller.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/background_task_runner.hpp)
  set_target_properties(nookbit_history_search_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_history_search_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_history_search_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_history_search_tests PRIVATE nookbit_history Qt6::Core Qt6::Sql)
  if(MSVC)
    target_compile_options(nookbit_history_search_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_history_search_tests)
  nookbit_add_qt_test(nookbit_application_version PLATFORM_PLUGINS COMMAND nookbit --version)
  string(REPLACE "." "\\." nookbit_version_pattern "${NOOKBIT_VERSION}")
  set_tests_properties(nookbit_application_version PROPERTIES
    PASS_REGULAR_EXPRESSION "NookBit ${nookbit_version_pattern}"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  add_executable(nookbit_feature_settings_tests feature_settings_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/feature_startup.cpp
    ${PROJECT_SOURCE_DIR}/src/app/feature_preferences.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/feature_settings_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/feature_settings_controller.cpp)
  set_target_properties(nookbit_feature_settings_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_feature_settings_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_feature_settings_tests PRIVATE nookbit_core Qt6::Core)
  if(MSVC)
    target_compile_options(nookbit_feature_settings_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_feature_settings_tests)
  add_executable(nookbit_feature_startup_tests feature_startup_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/feature_startup.cpp)
  target_include_directories(nookbit_feature_startup_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_feature_startup_tests PRIVATE nookbit_core Qt6::Core)
  nookbit_add_qt_test(nookbit_feature_startup_tests)
  # 활성 요청의 모든 조합을 사용자 설정과 분리된 실제 앱으로 시작해 검사한다.
  foreach(mask RANGE 0 31)
    set(feature_arguments --verify-feature-startup)
    set(feature_bit 1)
    foreach(feature file-organizer folder-watch notifications companion companion-feeding)
      math(EXPR feature_enabled "${mask} & ${feature_bit}")
      if(NOT feature_enabled)
        list(APPEND feature_arguments "--disable-feature=${feature}")
      endif()
      math(EXPR feature_bit "${feature_bit} * 2")
    endforeach()
    nookbit_add_qt_test(nookbit_feature_startup_${mask} PLATFORM_PLUGINS COMMAND nookbit_ui_checks ${feature_arguments})
    set_tests_properties(nookbit_feature_startup_${mask} PROPERTIES TIMEOUT 45
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endforeach()
  foreach(saved_case all none no_organizer no_companion)
    set(saved_arguments --verify-feature-preferences)
    if(saved_case STREQUAL "none")
      list(APPEND saved_arguments --disable-feature=all)
    elseif(saved_case STREQUAL "no_organizer")
      list(APPEND saved_arguments --disable-feature=file-organizer)
    elseif(saved_case STREQUAL "no_companion")
      list(APPEND saved_arguments --disable-feature=companion)
    endif()
    nookbit_add_qt_test(nookbit_saved_features_${saved_case} PLATFORM_PLUGINS COMMAND nookbit_ui_checks ${saved_arguments})
    set_tests_properties(nookbit_saved_features_${saved_case} PROPERTIES TIMEOUT 45
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endforeach()
  add_test(NAME nookbit_ui_metrics_contract COMMAND ${CMAKE_COMMAND}
    "-DNOOKBIT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ui_metrics_contract.cmake")
  add_executable(nookbit_qml_page_boundaries_tests qml_page_boundaries_tests.cpp)
  add_executable(nookbit_qml_rule_editor_tests qml_rule_editor_tests.cpp)
  add_executable(nookbit_qml_companion_edit_tests qml_companion_edit_tests.cpp)
  target_compile_definitions(nookbit_qml_companion_edit_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_qml_companion_edit_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  if(MSVC)
    target_compile_options(nookbit_qml_companion_edit_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  nookbit_add_qt_test(nookbit_qml_companion_edit_tests PLATFORM_PLUGINS)
  set_tests_properties(nookbit_qml_companion_edit_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  target_compile_definitions(nookbit_qml_rule_editor_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_qml_rule_editor_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  nookbit_add_qt_test(nookbit_qml_rule_editor_tests)
  set_tests_properties(nookbit_qml_rule_editor_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
  target_compile_definitions(nookbit_qml_page_boundaries_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_qml_page_boundaries_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  nookbit_add_qt_test(nookbit_qml_page_boundaries_tests)
  set_tests_properties(nookbit_qml_page_boundaries_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
  if(WIN32)
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_QUICK_BACKEND=set:software")
  else()
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()
endif()

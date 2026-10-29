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
add_executable(nookbit_classification_query_tests classification_query_tests.cpp)
add_executable(nookbit_notification_center_tests notification_center_tests.cpp)
add_executable(nookbit_companion_runtime_tests companion_runtime_tests.cpp)
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
  nookbit_classification_query_tests
  nookbit_notification_center_tests
  nookbit_companion_runtime_tests
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
add_test(NAME nookbit_classification_query_tests COMMAND nookbit_classification_query_tests)
add_test(NAME nookbit_notification_center_tests COMMAND nookbit_notification_center_tests)
add_test(NAME nookbit_companion_runtime_tests COMMAND nookbit_companion_runtime_tests)
add_test(NAME nookbit_interrupted_plan_tests COMMAND nookbit_interrupted_plan_tests)
add_test(NAME nookbit_operation_timing_tests COMMAND nookbit_operation_timing_tests)
add_test(NAME nookbit_operation_progress_tests COMMAND nookbit_operation_progress_tests)

if(TARGET nookbit_json_config)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Core Sql Gui Widgets Qml Quick)
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
  add_test(NAME nookbit_companion_tray_tests COMMAND nookbit_companion_tray_tests)
  set_tests_properties(nookbit_companion_tray_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    set_tests_properties(nookbit_companion_tray_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_ui_page_registry_tests COMMAND nookbit_ui_page_registry_tests)
  if(WIN32)
    set_tests_properties(nookbit_ui_page_registry_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  target_link_libraries(nookbit_owned_runtime_tests PRIVATE nookbit_filesystem nookbit_json_config nookbit_history Qt6::Core Qt6::Sql Qt6::Gui Qt6::Widgets)
  if(MSVC)
    target_compile_options(nookbit_owned_runtime_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  add_test(NAME nookbit_owned_runtime_tests COMMAND nookbit_owned_runtime_tests)
  set_tests_properties(nookbit_owned_runtime_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 45)
  if(WIN32)
    set_tests_properties(nookbit_owned_runtime_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_feature_runtime_tests COMMAND nookbit_feature_runtime_tests)
  if(WIN32)
    set_tests_properties(nookbit_feature_runtime_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_feature_connections_tests COMMAND nookbit_feature_connections_tests)
  if(WIN32)
    set_tests_properties(nookbit_feature_connections_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_directory_bundle_tests directory_bundle_tests.cpp)
  target_include_directories(nookbit_directory_bundle_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_compile_definitions(nookbit_directory_bundle_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_directory_bundle_tests PRIVATE nookbit_filesystem nookbit_history Qt6::Core Qt6::Sql)
  if(MSVC)
    target_compile_options(nookbit_directory_bundle_tests PRIVATE /W4 /permissive- /utf-8)
  endif()
  add_test(NAME nookbit_directory_bundle_tests COMMAND nookbit_directory_bundle_tests)
  if(WIN32)
    set_tests_properties(nookbit_directory_bundle_tests PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_companion_motion_tests COMMAND nookbit_companion_motion_tests)
  if(WIN32)
    set_tests_properties(nookbit_companion_motion_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_test(NAME nookbit_companion_interaction_controller_tests COMMAND nookbit_companion_interaction_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_companion_interaction_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_notification_controller_tests notification_controller_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.hpp
    ${PROJECT_SOURCE_DIR}/src/presentation/notification_controller.cpp)
  set_target_properties(nookbit_notification_controller_tests PROPERTIES AUTOMOC ON)
  target_include_directories(nookbit_notification_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_notification_controller_tests PRIVATE nookbit_core Qt6::Core)
  add_test(NAME nookbit_notification_controller_tests COMMAND nookbit_notification_controller_tests)
  if(WIN32)
    set_tests_properties(nookbit_notification_controller_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
  add_executable(nookbit_user_guide_tests user_guide_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/presentation/user_guide_controller.hpp
    ${PROJECT_SOURCE_DIR}/resources/config.qrc)
  set_target_properties(nookbit_user_guide_tests PROPERTIES AUTOMOC ON AUTORCC ON)
  target_include_directories(nookbit_user_guide_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_user_guide_tests PRIVATE nookbit_json_config Qt6::Gui)
  add_test(NAME nookbit_user_guide_tests COMMAND nookbit_user_guide_tests)
  if(WIN32)
    set_tests_properties(nookbit_user_guide_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
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
    nookbit_filesystem nookbit_json_config nookbit_history Qt6::Core)
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
  add_test(NAME nookbit_application_version COMMAND nookbit --version)
  string(REPLACE "." "\\." nookbit_version_pattern "${NOOKBIT_VERSION}")
  set_tests_properties(nookbit_application_version PROPERTIES
    PASS_REGULAR_EXPRESSION "NookBit ${nookbit_version_pattern}"
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(WIN32)
    set_tests_properties(nookbit_application_version PROPERTIES ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
  endif()
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
  add_test(NAME nookbit_feature_settings_tests COMMAND nookbit_feature_settings_tests)
  if(WIN32)
    set_tests_properties(nookbit_feature_settings_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  add_executable(nookbit_feature_startup_tests feature_startup_tests.cpp
    ${PROJECT_SOURCE_DIR}/src/app/feature_startup.cpp)
  target_include_directories(nookbit_feature_startup_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
  target_link_libraries(nookbit_feature_startup_tests PRIVATE nookbit_core Qt6::Core)
  add_test(NAME nookbit_feature_startup_tests COMMAND nookbit_feature_startup_tests)
  if(WIN32)
    set_tests_properties(nookbit_feature_startup_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  # 활성 요청의 모든 조합을 사용자 설정과 분리된 실제 앱으로 시작해 검사한다.
  foreach(mask RANGE 0 15)
    set(feature_arguments --verify-feature-startup)
    set(feature_bit 1)
    foreach(feature file-organizer folder-watch notifications companion)
      math(EXPR feature_enabled "${mask} & ${feature_bit}")
      if(NOT feature_enabled)
        list(APPEND feature_arguments "--disable-feature=${feature}")
      endif()
      math(EXPR feature_bit "${feature_bit} * 2")
    endforeach()
    add_test(NAME nookbit_feature_startup_${mask} COMMAND nookbit ${feature_arguments})
    set_tests_properties(nookbit_feature_startup_${mask} PROPERTIES TIMEOUT 45
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(WIN32)
      set_tests_properties(nookbit_feature_startup_${mask} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    endif()
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
    add_test(NAME nookbit_saved_features_${saved_case} COMMAND nookbit ${saved_arguments})
    set_tests_properties(nookbit_saved_features_${saved_case} PROPERTIES TIMEOUT 45
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    if(WIN32)
      set_tests_properties(nookbit_saved_features_${saved_case} PROPERTIES ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms")
    endif()
  endforeach()
  add_test(NAME nookbit_ui_metrics_contract COMMAND ${CMAKE_COMMAND}
    "-DNOOKBIT_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/ui_metrics_contract.cmake")
  add_executable(nookbit_qml_page_boundaries_tests qml_page_boundaries_tests.cpp)
  add_executable(nookbit_qml_rule_editor_tests qml_rule_editor_tests.cpp)
  target_compile_definitions(nookbit_qml_rule_editor_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_qml_rule_editor_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  add_test(NAME nookbit_qml_rule_editor_tests COMMAND nookbit_qml_rule_editor_tests)
  set_tests_properties(nookbit_qml_rule_editor_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
  target_compile_definitions(nookbit_qml_page_boundaries_tests PRIVATE NOOKBIT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
  target_link_libraries(nookbit_qml_page_boundaries_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick)
  add_test(NAME nookbit_qml_page_boundaries_tests COMMAND nookbit_qml_page_boundaries_tests)
  set_tests_properties(nookbit_qml_page_boundaries_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_QUICK_CONTROLS_STYLE=Basic")
  if(WIN32)
    set_tests_properties(nookbit_qml_rule_editor_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    set_tests_properties(nookbit_qml_page_boundaries_tests PROPERTIES
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  endif()
  if(WIN32)
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=set:$<TARGET_FILE_DIR:Qt6::Core>/../plugins/platforms;QT_QUICK_BACKEND=set:software")
  else()
    set_tests_properties(nookbit_qml_smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
  endif()
endif()

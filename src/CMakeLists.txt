add_library(nookbit_core STATIC
  app/composition_root.cpp
  application/use_cases/create_preview_use_case.cpp
  application/use_cases/content_fingerprint.cpp
  application/use_cases/approve_plan_use_case.cpp
  application/use_cases/freeze_preview_use_case.cpp
  application/use_cases/plan_signature.cpp
  application/use_cases/prepare_transfer_plan_use_case.cpp
  application/use_cases/verify_saved_plan_use_case.cpp
  application/use_cases/evaluate_watch_folder_use_case.cpp
  application/use_cases/classify_files_use_case.cpp
  application/use_cases/test_rule_use_case.cpp
  application/use_cases/transfer_validation.cpp
  application/use_cases/commit_transfer_copy_use_case.cpp
  application/use_cases/execute_transfer_step_use_case.cpp
  application/use_cases/inspect_transfer_recovery_use_case.cpp
  application/use_cases/inspect_interrupted_plan_use_case.cpp
  application/use_cases/recover_interrupted_plan_use_case.cpp
  application/use_cases/resume_interrupted_transfer_use_case.cpp
  application/use_cases/reconcile_transfer_recovery_use_case.cpp
  application/use_cases/resume_committed_transfer_use_case.cpp
  application/use_cases/resume_partial_copy_use_case.cpp
  application/use_cases/commit_resumed_staging_use_case.cpp
  application/use_cases/reconcile_transfer_commit_use_case.cpp
  application/use_cases/transfer_recovery_binding.cpp
  application/use_cases/cleanup_transfer_staging_use_case.cpp
    application/use_cases/cleanup_preserved_staging_use_case.cpp
  application/use_cases/finalize_transfer_use_case.cpp
  application/use_cases/rollback_transfer_use_case.cpp
  modules/rules/rule_engine.cpp
  modules/watch/watch_evaluator.cpp
  modules/watch/watch_file_readiness.cpp
  modules/character/character_presentation.cpp
  modules/character/image_pack.cpp
  modules/character/behavior_manager.cpp
  modules/character/interaction_manager.cpp
  modules/character/idle_walk_state.cpp
  modules/notifications/notification_center.cpp
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
  infrastructure/diagnostics/operation_timing.cpp
  infrastructure/filesystem/filesystem_scanner.cpp
  infrastructure/filesystem/file_identity.cpp
  infrastructure/filesystem/operation_mutex.cpp
  infrastructure/filesystem/recovery_service.cpp
  infrastructure/filesystem/interrupted_operation_recovery_service.cpp
  infrastructure/filesystem/windows_file_mover.cpp
  infrastructure/filesystem/windows_directory_mover.cpp
  infrastructure/filesystem/windows_staged_copy.cpp
  infrastructure/filesystem/transfer_recovery_service.cpp
  infrastructure/filesystem/safe_executor.cpp
  infrastructure/filesystem/file_operation_service.cpp
)
target_include_directories(nookbit_filesystem PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nookbit_filesystem PUBLIC nookbit_core)
target_compile_features(nookbit_filesystem PUBLIC cxx_std_20)
if(WIN32)
  target_link_libraries(nookbit_filesystem PRIVATE bcrypt)
endif()

if(MSVC)
  target_compile_options(nookbit_core PRIVATE /W4 /permissive- /utf-8)
  target_compile_options(nookbit_filesystem PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(nookbit_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
  target_compile_options(nookbit_filesystem PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()

if(NOOKBIT_BUILD_QT_APP)
  find_package(Qt6 6.5 QUIET COMPONENTS Core Gui Widgets Qml Quick QuickControls2 QuickDialogs2 Sql)

  if(Qt6_FOUND)
    qt_standard_project_setup(REQUIRES 6.5)

    add_library(nookbit_json_config STATIC
      infrastructure/configuration/json_document_loader.cpp
      infrastructure/configuration/json_settings_repository.cpp
      infrastructure/configuration/json_watch_settings_repository.cpp
      infrastructure/configuration/json_pending_files_repository.cpp
      infrastructure/configuration/json_rule_set_repository.cpp
      infrastructure/configuration/json_character_presentation.cpp
      infrastructure/configuration/json_image_pack_loader.cpp
      infrastructure/configuration/directory_image_pack_library.cpp
    )
    target_include_directories(nookbit_json_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nookbit_json_config PUBLIC nookbit_core Qt6::Core PRIVATE Qt6::Gui)

    add_library(nookbit_history STATIC
      infrastructure/cryptography/qt_plan_hasher.cpp
      infrastructure/persistence_sqlite/sqlite_history_repository.cpp
      infrastructure/persistence_sqlite/sqlite_schema.cpp
      infrastructure/persistence_sqlite/sqlite_transfer_journal.cpp
    )
    target_include_directories(nookbit_history PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(nookbit_history PUBLIC nookbit_core Qt6::Core Qt6::Sql)
    if(WIN32)
      target_link_libraries(nookbit_history PRIVATE bcrypt)
    endif()

    qt_add_executable(nookbit
      presentation/main.cpp
      app/pending_queue_adapter.hpp
      app/feature_connections.hpp
      app/feature_connections.cpp
      presentation/background_task_runner.hpp
      presentation/character_controller.hpp
      presentation/character_controller.cpp
      presentation/character_settings_controller.hpp
      presentation/character_settings_controller.cpp
      presentation/companion_edit_controller.hpp
      presentation/companion_edit_controller.cpp
      presentation/notification_controller.hpp
      presentation/notification_controller.cpp
      presentation/companion_interaction_controller.hpp
      presentation/companion_interaction_controller.cpp
      presentation/companion_motion_controller.hpp
      presentation/companion_motion_controller.cpp
      presentation/character_window_controller.hpp
      presentation/app_lifecycle_controller.hpp
      presentation/ui_settings_controller.hpp
      presentation/destination_settings_controller.hpp
      presentation/user_guide_controller.hpp
      presentation/file_icon_provider.hpp
      presentation/file_preview_provider.hpp
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
    if(WIN32)
      enable_language(RC)
      configure_file(
        "${PROJECT_SOURCE_DIR}/resources/icons/nookbit.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/nookbit.rc"
        @ONLY)
      target_sources(nookbit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/nookbit.rc")
      # 아이콘 교체만으로도 실행 파일의 Windows 리소스를 다시 컴파일한다.
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/nookbit.rc" PROPERTIES
        OBJECT_DEPENDS "${PROJECT_SOURCE_DIR}/resources/icons/nookbit.ico")
    endif()
    set_property(TARGET nookbit APPEND PROPERTY LINK_DEPENDS
      "${PROJECT_SOURCE_DIR}/resources/imagepacks/test/imagepack.json"
      "${PROJECT_SOURCE_DIR}/resources/imagepacks/test/images/test-character.png")
    file(GLOB NOOKBIT_NMOO_ASSETS CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/resources/imagepacks/nmoo/images/*.png")
    set_property(TARGET nookbit APPEND PROPERTY LINK_DEPENDS
      "${PROJECT_SOURCE_DIR}/resources/imagepacks/nmoo/imagepack.json" ${NOOKBIT_NMOO_ASSETS})
    add_custom_command(TARGET nookbit POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_SOURCE_DIR}/resources/imagepacks/test"
        "$<TARGET_FILE_DIR:nookbit>/imagepacks/test"
      COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${PROJECT_SOURCE_DIR}/resources/imagepacks/nmoo"
        "$<TARGET_FILE_DIR:nookbit>/imagepacks/nmoo"
      COMMENT "Deploying the test ImagePack"
      VERBATIM)
    target_include_directories(nookbit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    file(TO_CMAKE_PATH "${NOOKBIT_DEV_DESTINATION_ROOT}" NOOKBIT_DEV_DESTINATION_PATH)
    target_compile_definitions(nookbit PRIVATE
      NOOKBIT_DEV_DESTINATION_ROOT="${NOOKBIT_DEV_DESTINATION_PATH}")

    set_source_files_properties(presentation/qml/UiMetrics.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    set_source_files_properties(presentation/qml/UiTheme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    qt_add_qml_module(nookbit
      URI NookBit
      VERSION 1.0
      QML_FILES
        presentation/qml/Main.qml
        presentation/qml/PageSurface.qml
        presentation/qml/OperationProgressPopup.qml
        presentation/qml/CollisionSettingsPage.qml
          presentation/qml/CollisionPolicyPicker.qml
        presentation/qml/NavigationButton.qml
        presentation/qml/WatchSettingsPage.qml
          presentation/qml/WatchStatusCard.qml
          presentation/qml/WatchPatternEditor.qml
          presentation/qml/SettingsWorkflow.qml
        presentation/qml/HistoryPage.qml
          presentation/qml/HistoryTimeline.qml
          presentation/qml/HistoryDetail.qml
          presentation/qml/FileTypeBadge.qml
        presentation/qml/RecoveryPage.qml
        presentation/qml/CompanionPage.qml
          presentation/qml/CompanionSettingsPanel.qml
          presentation/qml/AppSwitch.qml
          presentation/qml/AppSlider.qml
        presentation/qml/AppSettingsPage.qml
        presentation/qml/DeveloperToolsPage.qml
        presentation/qml/LegacyRuleEditorDialog.qml
        presentation/qml/WatchProposalDialog.qml
        presentation/qml/SortConfirmationDialog.qml
        presentation/qml/UiMetrics.qml
        presentation/qml/UiTheme.qml
        presentation/qml/GuideDownload.qml
        presentation/qml/AppButton.qml
        presentation/qml/ButtonFeedback.qml
        presentation/qml/LargeActionButton.qml
        presentation/qml/BackButton.qml
        presentation/qml/NavigationIcon.qml
        presentation/qml/HomeMenuButton.qml
        presentation/qml/SettingsSection.qml
        presentation/qml/AppComboBox.qml
        presentation/qml/DropdownIndicator.qml
        presentation/qml/AppTextField.qml
        presentation/qml/AppSpinBox.qml
        presentation/qml/CompactHome.qml
        presentation/qml/HomeStatusCard.qml
        presentation/qml/CompanionPortrait.qml
        presentation/qml/NotificationSettingsPage.qml
        presentation/qml/FileSortPage.qml
          presentation/qml/FileSortInput.qml
          presentation/qml/FileSortStepper.qml
        presentation/qml/FileSortPreview.qml
        presentation/qml/FileSortStatus.qml
        presentation/qml/FileSortConfirmation.qml
        presentation/qml/CharacterBadge.qml
        presentation/qml/CharacterSprite.qml
        presentation/qml/CharacterAnchorEditor.qml
        presentation/qml/AnchorHandle.qml
        presentation/qml/CompanionPreview.qml
        presentation/qml/CompanionCoordinateEditor.qml
        presentation/qml/CompanionTimeline.qml
        presentation/qml/CompanionPlayback.qml
        presentation/qml/CompanionPlaybackClock.qml
        presentation/qml/PlaybackIconButton.qml
        presentation/qml/CharacterWindow.qml
        presentation/qml/CharacterBubble.qml
        presentation/qml/CharacterBubbleView.qml
        presentation/qml/RuleEditorPage.qml
        presentation/qml/RuleEditorState.qml
        presentation/qml/RuleConnectionGeometry.js
        presentation/qml/RuleEditorButton.qml
        presentation/qml/RuleEditorMenu.qml
        presentation/qml/RuleNodeCard.qml
        presentation/qml/RuleCanvas.qml
        presentation/qml/RuleInspector.qml
        presentation/qml/RuleTestPanel.qml
        presentation/qml/RuleEditorToolbar.qml
        presentation/qml/RuleDocumentActions.qml
    )

    target_link_libraries(nookbit PRIVATE
      nookbit_core
      nookbit_filesystem
      nookbit_json_config
      nookbit_history
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      Qt6::Qml
      Qt6::Quick
      Qt6::QuickControls2
      Qt6::QuickDialogs2
      Qt6::Sql
    )
    if(WIN32)
      target_link_libraries(nookbit PRIVATE shell32 ole32 user32)
      find_program(NOOKBIT_WINDEPLOYQT NAMES windeployqt windeployqt6
        HINTS "${Qt6_DIR}/../../../bin")
    endif()
    if(NOOKBIT_WINDEPLOYQT)
      add_custom_target(nookbit_package
        COMMAND "${CMAKE_COMMAND}"
          "-DNOOKBIT_EXECUTABLE=$<TARGET_FILE:nookbit>"
          "-DNOOKBIT_DEPLOY_TOOL=${NOOKBIT_WINDEPLOYQT}"
          "-DNOOKBIT_QML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/presentation/qml"
          "-DNOOKBIT_PACKAGE_DIR=${PROJECT_SOURCE_DIR}/out/package/NookBit-$<CONFIG>"
          "-DNOOKBIT_BUILD_CONFIG=$<CONFIG>"
          "-DNOOKBIT_COMPACT_PACKAGE=${NOOKBIT_COMPACT_PACKAGE}"
          "-DNOOKBIT_TEST_PACK=${PROJECT_SOURCE_DIR}/resources/imagepacks/test"
          "-DNOOKBIT_NMOO_PACK=${PROJECT_SOURCE_DIR}/resources/imagepacks/nmoo"
          -P "${PROJECT_SOURCE_DIR}/cmake/package_windows.cmake"
        DEPENDS nookbit
        COMMENT "Packaging the NookBit Windows runtime"
        VERBATIM)
      add_custom_command(TARGET nookbit POST_BUILD
        COMMAND "${NOOKBIT_WINDEPLOYQT}"
          $<$<CONFIG:Debug>:--debug>$<$<NOT:$<CONFIG:Debug>>:--release>
          --no-translations --qmldir ${CMAKE_CURRENT_SOURCE_DIR}/presentation/qml
          $<TARGET_FILE:nookbit>
        COMMENT "Deploying Qt runtime and QML imports for NookBit"
        VERBATIM)
    endif()
  else()
    message(STATUS "Qt 6.5+ not found: building the tested C++ core only")
  endif()
endif()

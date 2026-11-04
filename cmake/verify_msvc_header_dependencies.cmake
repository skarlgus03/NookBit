# 기능 조립부가 실제로 포함하는 실행·진단 헤더의 의존성을 확인한다.
execute_process(
  COMMAND "${NOOKBIT_NINJA}" -C "${NOOKBIT_BUILD_DIR}" -t deps
    src/CMakeFiles/nookbit.dir/app/file_organizer_runtime.cpp.obj
  RESULT_VARIABLE result OUTPUT_VARIABLE dependencies ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR
   NOT dependencies MATCHES "app/thread_local_operations[.]hpp" OR
   NOT dependencies MATCHES "infrastructure/diagnostics/operation_timing[.]hpp")
  message(FATAL_ERROR "Actual app header dependencies are missing.\n${dependencies}\n${error}")
endif()

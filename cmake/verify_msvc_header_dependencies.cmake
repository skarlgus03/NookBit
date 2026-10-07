# 테스트용 구현뿐 아니라 실제 앱 시작 객체의 헤더 의존성도 확인한다.
execute_process(
  COMMAND "${NOOKBIT_NINJA}" -C "${NOOKBIT_BUILD_DIR}" -t deps
    src/CMakeFiles/nookbit.dir/presentation/main.cpp.obj
  RESULT_VARIABLE result OUTPUT_VARIABLE dependencies ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR
   NOT dependencies MATCHES "app/thread_local_operations[.]hpp" OR
   NOT dependencies MATCHES "infrastructure/diagnostics/operation_timing[.]hpp")
  message(FATAL_ERROR "Actual app header dependencies are missing.\n${dependencies}\n${error}")
endif()

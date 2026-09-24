#include "infrastructure/filesystem/operation_mutex.hpp"

namespace nookbit {

// 획득한 잠금과 운영체제 핸들을 돌려주는 소멸자.
OperationMutex::~OperationMutex() {
#ifdef _WIN32
    if (acquired_) ReleaseMutex(handle_);
    if (handle_) CloseHandle(handle_);
#endif
}

// 최대 5초 동안 다른 실행·복구 작업이 끝나기를 기다리는 함수.
Result<void> OperationMutex::acquire() {
#ifdef _WIN32
    if (acquired_) return Result<void>::success();
    handle_ = CreateMutexW(nullptr, FALSE, L"Local\\NookBit.FileOperations.v1");
    if (!handle_) {
        return Result<void>::failure({"operation.lock_failed", "Cannot create operation mutex."});
    }
    const DWORD outcome = WaitForSingleObject(handle_, 5000);
    if (outcome != WAIT_OBJECT_0 && outcome != WAIT_ABANDONED) {
        return Result<void>::failure({
            "operation.busy", "Another NookBit file operation is active or unavailable.",
        });
    }
    acquired_ = true;
    return Result<void>::success();
#else
    return Result<void>::failure({"operation.unsupported_platform", "Windows is required."});
#endif
}

} // namespace nookbit

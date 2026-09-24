#pragma once

#include <nookbit/domain/result.hpp>

#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#endif

namespace nookbit {

/// 여러 앱 인스턴스의 실행·복구가 겹치지 않게 하는 Windows 시스템 잠금.
class OperationMutex final {
public:
    /// 아직 잠그지 않은 시스템 잠금 객체를 만드는 생성자.
    OperationMutex() = default;

    /// 획득한 잠금과 운영체제 핸들을 돌려주는 소멸자.
    ~OperationMutex();

    OperationMutex(const OperationMutex&) = delete;
    OperationMutex& operator=(const OperationMutex&) = delete;

    /// 최대 5초 동안 다른 실행·복구 작업이 끝나기를 기다리는 함수.
    [[nodiscard]] Result<void> acquire();

private:
#ifdef _WIN32
    HANDLE handle_{nullptr};
#endif
    bool acquired_{false};
};

} // namespace nookbit

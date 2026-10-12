#pragma once
#include <nookbit/application/ports.hpp>
#include <mutex>

namespace nookbit {
/// 앱에서 선택한 현재 정리 설정을 복구 재검증에 제공하며 디스크에는 저장하지 않는다.
class RuntimeSettingsRepository final : public ISettingsRepository {
public:
    /// 앱 시작의 목적지와 공통 정책을 현재 설정으로 보관하는 함수.
    explicit RuntimeSettingsRepository(UserSettings initial) : value_(std::move(initial)) {}
    /// 현재 설정을 동시 변경과 분리된 스냅샷으로 읽는 함수.
    Result<UserSettings> load() const override { std::lock_guard lock(mutex_); return Result<UserSettings>::success(value_); }
    /// 디스크 저장은 앱 설정 저장 계층에 맡기고 여기서는 거부하는 함수.
    Result<void> save(const UserSettings&) override { return Result<void>::failure({"settings.read_only", "Runtime settings are not persisted."}); }
    /// 앱이 수락한 목적지를 현재 복구 재검증 설정에도 반영하는 함수.
    void setHomeRoot(std::filesystem::path root) { std::lock_guard lock(mutex_); value_.homeRoot = std::move(root); }
    /// 앱에서 바꾼 공통 충돌 정책을 현재 설정에 반영하는 함수.
    void setCollisionPolicy(CollisionPolicy policy) { std::lock_guard lock(mutex_); value_.defaultCollisionPolicy = policy; }
    /// 사용자가 허용한 외부 목적지를 현재 실행·복구 안전 검사에도 반영하는 함수.
    void setAllowedDestinationRoots(std::vector<std::filesystem::path> roots) {
        std::lock_guard lock(mutex_); value_.allowedDestinationRoots = std::move(roots);
    }
private:
    mutable std::mutex mutex_;
    UserSettings value_;
};
}

#pragma once

#include <nookbit/application/ports.hpp>

#include <utility>

namespace nookbit {

/// 한 번의 화면 작업에만 사용할 목적지 설정을 제공하는 저장소.
class SessionSettingsRepository final : public ISettingsRepository {
public:
    /// 이번 작업의 목적지 루트와 공통 충돌 정책을 보관하는 생성자.
    explicit SessionSettingsRepository(std::filesystem::path homeRoot,
                                       CollisionPolicy collisionPolicy = CollisionPolicy::Rename) {
        settings_.homeRoot = std::move(homeRoot);
        settings_.defaultCollisionPolicy = collisionPolicy;
    }

    /// 이번 작업의 목적지 루트와 기본 안전 정책을 반환하는 함수.
    [[nodiscard]] Result<UserSettings> load() const override {
        return Result<UserSettings>::success(settings_);
    }

    /// 임시 설정을 파일에 저장하지 않는 함수.
    [[nodiscard]] Result<void> save(const UserSettings&) override {
        return Result<void>::failure({"settings.read_only", "Session settings are not persisted."});
    }

private:
    UserSettings settings_;
};

} // namespace nookbit

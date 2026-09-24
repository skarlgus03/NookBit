#pragma once

#include <nookbit/application/ports.hpp>

#include <QString>

namespace nookbit {

/// 설정 JSON을 검증한 뒤 코어 설정으로 변환하는 저장소.
class JsonSettingsRepository final : public ISettingsRepository {
public:
    /// 읽을 설정 파일과 검증 Schema 경로를 지정하는 생성자.
    JsonSettingsRepository(QString settingsPath, QString schemaPath);

    /// 설정 파일을 검증하고 활성 감시 폴더를 반환하는 함수.
    [[nodiscard]] Result<UserSettings> load() const override;

    /// 아직 지원하지 않는 설정 저장 요청을 명시적으로 거부하는 함수.
    [[nodiscard]] Result<void> save(const UserSettings& settings) override;

private:
    QString settingsPath_;
    QString schemaPath_;
};

} // namespace nookbit

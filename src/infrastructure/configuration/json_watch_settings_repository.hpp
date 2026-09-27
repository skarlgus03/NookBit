#pragma once

#include <nookbit/application/ports.hpp>

#include <QString>

namespace nookbit {

/// 감시 조건을 별도 JSON 파일에 원자적으로 저장한다.
class JsonWatchSettingsRepository final : public IWatchSettingsRepository {
public:
    /// 설정 파일과 검증용 Schema 경로를 보관하는 생성자.
    JsonWatchSettingsRepository(QString settingsPath, QString schemaPath);

    /// 저장된 감시 폴더와 조건을 검증해 읽는 함수.
    [[nodiscard]] Result<WatchSettings> load() const override;

    /// 감시 폴더와 조건을 검증해 한 번에 저장하는 함수.
    [[nodiscard]] Result<void> save(const WatchSettings& settings) const override;

private:
    QString settingsPath_;
    QString schemaPath_;
};

} // namespace nookbit

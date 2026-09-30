#pragma once

#include <nookbit/application/ports.hpp>

#include <QString>

namespace nookbit {

/// 분류 규칙 JSON을 검증한 뒤 코어 규칙 모델로 변환하는 저장소.
class JsonRuleSetRepository final : public IRuleSetRepository, public IRuleSetFileReader,
                                    public IRuleSetWriter,
                                    public IRuleSetMigrator {
public:
    /// 읽을 규칙 파일과 검증 Schema 경로를 지정하는 생성자.
    JsonRuleSetRepository(QString rulesPath, QString schemaPath);
    /// 새 규칙과 기존 규칙의 Schema를 함께 지정하는 생성자.
    JsonRuleSetRepository(QString rulesPath, QString schemaPath,
                          QString legacySchemaPath);

    /// 규칙 파일을 검증하고 버전이 있는 규칙 묶음을 반환하는 함수.
    [[nodiscard]] Result<RuleSet> load() const override;

    /// 외부 JSON을 같은 Schema로 검사하되 현재 저장본은 건드리지 않는 함수.
    [[nodiscard]] Result<RuleSet> loadFile(
        const std::filesystem::path& path) const override;

    /// v2 또는 v3 규칙 트리를 검사하고 JSON 파일에 원자적으로 저장하는 함수.
    [[nodiscard]] Result<void> save(const RuleSet& rules) const override;

    /// 기존 파일을 백업하고 의미가 그대로 유지되는 v2 규칙만 v3로 옮기는 함수.
    [[nodiscard]] Result<void> migrateV2ToV3() const override;

private:
    QString rulesPath_;
    QString schemaPath_;
    QString legacySchemaPath_;
};

} // namespace nookbit

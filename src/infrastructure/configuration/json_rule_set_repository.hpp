#pragma once

#include <nookbit/application/ports.hpp>

#include <QString>

namespace nookbit {

/// 분류 규칙 JSON을 검증한 뒤 코어 규칙 모델로 변환하는 저장소.
class JsonRuleSetRepository final : public IRuleSetRepository {
public:
    /// 읽을 규칙 파일과 검증 Schema 경로를 지정하는 생성자.
    JsonRuleSetRepository(QString rulesPath, QString schemaPath);

    /// 규칙 파일을 검증하고 버전이 있는 규칙 묶음을 반환하는 함수.
    [[nodiscard]] Result<RuleSet> load() const override;

private:
    QString rulesPath_;
    QString schemaPath_;
};

} // namespace nookbit

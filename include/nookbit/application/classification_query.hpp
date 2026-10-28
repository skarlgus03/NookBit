#pragma once
#include <nookbit/domain/model.hpp>

namespace nookbit {
/// 요청된 파일 하나의 읽기 전용 판단. 결정이 없으면 미분류이며 제외도 결정에 명시된다.
struct FileClassification {
    std::filesystem::path source;
    std::optional<RuleDecision> decision;
};

/// 하나의 활성 규칙 복사본으로 판단한 결과이며 이동 계획이나 승인을 포함하지 않는다.
struct ClassificationSnapshot {
    std::string rulesRevision;
    std::vector<FileClassification> files;
};

class IClassificationQuery {
public:
    /// 조회 구현체를 인터페이스로 안전하게 해제하는 함수.
    virtual ~IClassificationQuery() = default;
    /// 입력 순서대로 한 개씩 판단하며 비어 있지 않은 규칙 식별값을 함께 반환하는 함수.
    /// 파일 내용 조회·스캔·저장·이동 없이 전달된 메타데이터만 사용한다.
    [[nodiscard]] virtual Result<ClassificationSnapshot> classify(
        const std::vector<FileFact>& files, std::int64_t nowUtcMs) const = 0;
};
}

#pragma once
#include <nookbit/application/classification_query.hpp>
#include <nookbit/application/ports.hpp>

namespace nookbit {
/// 파일 정리 기능이 외부 모듈에 제공하는 읽기 전용 분류 조회.
class ClassifyFilesUseCase final : public IClassificationQuery {
public:
    /// 활성 규칙 저장소·판단기·규칙 식별값 해시 기능을 연결하는 생성자.
    ClassifyFilesUseCase(IRuleSetRepository& rules, IRuleEngine& engine, IPlanHasher& hasher);
    /// 한 규칙 복사본과 첫 일치 정책으로 입력 파일들을 판단하는 함수.
    [[nodiscard]] Result<ClassificationSnapshot> classify(
        const std::vector<FileFact>& files, std::int64_t nowUtcMs) const override;
private:
    IRuleSetRepository& rules_;
    IRuleEngine& engine_;
    IPlanHasher& hasher_;
};
}

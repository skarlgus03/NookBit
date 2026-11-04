#pragma once
#include <nookbit/application/ports.hpp>

namespace nookbit {
/// 이전 고정 프리셋을 수정 가능한 기본 노드들로 펼치는 함수.
RuleSet::FolderFlow expandedFlow(const RuleSet::FolderFlow& flow);
/// 모든 노드와 호출의 연결·입력 종류·순환을 파일 I/O 없이 검사하는 함수.
Result<void> validateProcessingFlows(const RuleSet& rules);
/// 고정된 원본 집합에 저장된 흐름 문맥을 재적용하는 함수.
Result<void> restoreProcessingSources(ScanSnapshot& snapshot, const OperationPlan& plan);
}

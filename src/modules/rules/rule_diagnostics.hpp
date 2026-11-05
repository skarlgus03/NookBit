#pragma once

#include <nookbit/domain/model.hpp>

namespace nookbit {

struct RuleWarning {
    std::string flowId;
    std::string nodeId;
    // 비어 있으면 조건 모순이고, 값이 있으면 먼저 선택되는 형제 노드다.
    std::string earlierNodeId;
};

// 검증된 규칙에서 도달 불가가 증명된 가지를 찾는 함수.
std::vector<RuleWarning> inspectRules(const RuleSet& rules);

}

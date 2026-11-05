#pragma once

#include <nookbit/domain/model.hpp>
#include <string_view>

namespace nookbit {

// 규칙 파일과 편집 그래프의 연산 이름을 모델의 연산으로 변환하는 함수.
inline std::optional<LogicalOperator> logicalOperatorFor(std::string_view type) {
    if (type == "all") return LogicalOperator::All;
    if (type == "any") return LogicalOperator::Any;
    if (type == "not") return LogicalOperator::Not;
    return std::nullopt;
}

// 연산을 저장 형식에서 사용하는 이름으로 바꾸는 함수.
inline const char* logicalOperatorName(LogicalOperator operation) {
    switch (operation) {
    case LogicalOperator::All: return "all";
    case LogicalOperator::Any: return "any";
    case LogicalOperator::Not: return "not";
    }
    return "";
}

// 연산에 허용되는 입력 수인지 확인하는 함수.
inline bool validLogicalArity(LogicalOperator operation, std::size_t count) {
    return operation == LogicalOperator::Not ? count == 1 : count >= 2 && count <= 1000;
}

}

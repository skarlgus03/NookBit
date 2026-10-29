#pragma once
#include <nookbit/domain/model.hpp>
#include <nookbit/domain/result.hpp>
#include <optional>
#include <string>
#include <vector>

namespace nookbit {
enum class HistoryResultFilter { All, Completed, RolledBack, Problem };
enum class HistoryOrder { Newest, Oldest };
/// 같은 시각의 작업도 중복 없이 이어 읽기 위한 정렬 위치.
struct HistoryCursor {
    std::string activityUtc;
    PlanId planId;
};
/// 시각 범위는 시작 포함·끝 제외이며 파일을 읽거나 변경하지 않는 조회 조건.
struct HistorySearch {
    std::string text;
    std::optional<std::string> fromUtc, untilUtc;
    HistoryResultFilter result{HistoryResultFilter::All};
    HistoryOrder order{HistoryOrder::Newest};
    int limit{50};
    std::optional<HistoryCursor> after;
};
struct HistorySearchEntry {
    PlanHistoryEntry plan;
    int matchedFileCount{};
};
struct HistorySearchPage {
    std::vector<HistorySearchEntry> entries;
    std::optional<HistoryCursor> next;
};
/// 기록 검색만 제공하며 파일 실행과 승인 기능을 노출하지 않는 창구.
class IHistoryQuery {
public:
    /// 인터페이스를 통해 조회 구현을 안전하게 해제하는 소멸자.
    virtual ~IHistoryQuery() = default;
    /// 전체 저장 기록에서 조건에 맞는 작업을 제한된 개수만 반환하는 함수.
    [[nodiscard]] virtual Result<HistorySearchPage> searchHistory(const HistorySearch& query) const = 0;
};
}

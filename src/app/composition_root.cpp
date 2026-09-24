#include "app/composition_root.hpp"

#include <stdexcept>
#include <utility>

namespace nookbit {

// 미리보기와 실행에 필요한 서비스가 모두 연결되었는지 확인하는 함수.
bool ApplicationServices::complete() const noexcept {
    return scanner && ruleEngine && planner && executor && history && settings && rules && events;
}

CompositionRoot::CompositionRoot(ApplicationServices services)
    : services_(std::move(services)) {
    if (!services_.complete()) {
        throw std::invalid_argument("CompositionRoot requires every application service");
    }
}

const ApplicationServices& CompositionRoot::services() const noexcept {
    return services_;
}

} // namespace nookbit

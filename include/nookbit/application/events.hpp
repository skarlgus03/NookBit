#pragma once

#include <nookbit/domain/model.hpp>

#include <cstdint>
#include <string>
#include <variant>

namespace nookbit {

struct PlanCreated {
    PlanId planId;
};

struct PlanStatusChanged {
    PlanId planId;
    PlanStatus status;
};

struct StepStatusChanged {
    PlanId planId;
    StepId stepId;
    StepStatus status;
    std::optional<Error> failure;
};

struct HomeStateChanged {
    std::uint64_t unclassifiedFiles{0};
    std::uint64_t staleFiles{0};
    std::uint64_t largeFiles{0};
    std::uintmax_t totalBytes{0};
};

using DomainEvent = std::variant<
    PlanCreated,
    PlanStatusChanged,
    StepStatusChanged,
    HomeStateChanged
>;

class IEventSink {
public:
    virtual ~IEventSink() = default;
    virtual void publish(const DomainEvent& event) = 0;
};

} // namespace nookbit

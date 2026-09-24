#pragma once

#include <nookbit/application/ports.hpp>

#include <memory>

namespace nookbit {

struct ApplicationServices {
    std::shared_ptr<IScanner> scanner;
    std::shared_ptr<IRuleEngine> ruleEngine;
    std::shared_ptr<IPlanner> planner;
    std::shared_ptr<IExecutor> executor;
    std::shared_ptr<IHistoryRepository> history;
    std::shared_ptr<ISettingsRepository> settings;
    std::shared_ptr<IRuleSetRepository> rules;
    std::shared_ptr<IEventSink> events;

    [[nodiscard]] bool complete() const noexcept;
};

class CompositionRoot final {
public:
    explicit CompositionRoot(ApplicationServices services);

    [[nodiscard]] const ApplicationServices& services() const noexcept;

private:
    ApplicationServices services_;
};

} // namespace nookbit

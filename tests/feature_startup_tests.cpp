#include "app/feature_startup.hpp"
#include <QCoreApplication>
#include <iostream>

// 시작 선택과 의존성 해석이 저장 상태 없이 일관되는지 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    int failures{};
    const auto expect = [&failures](bool condition, const char* message) {
        if (!condition) { ++failures; std::cerr << message << '\n'; }
    };
    for (int mask = 0; mask < 16; ++mask) {
        const nookbit::FeatureSelection requested{bool(mask & 1), bool(mask & 2), bool(mask & 4), bool(mask & 8)};
        const auto plan = nookbit::resolveFeatureStartup(requested);
        expect(plan.active.organizer == requested.organizer && plan.active.notifications == requested.notifications
            && plan.active.companion == requested.companion, "optional features remain independent");
        expect(plan.active.watch == (requested.watch && requested.organizer), "watch requires Organizer");
        expect(plan.requested.watch == requested.watch, "dependency resolution preserves request");
        expect(plan.messages.isEmpty() == (!requested.watch || requested.organizer), "only blocked dependency has explanation");
    }
    const auto defaults = nookbit::parseFeatureSelection({"nookbit", "--unrelated"});
    expect(defaults && defaults.value().organizer && defaults.value().watch && defaults.value().notifications
        && defaults.value().companion, "default enables all features");
    const auto selected = nookbit::parseFeatureSelection({"--disable-feature=companion", "--disable-feature=file-organizer",
        "--disable-feature=notifications", "--disable-feature=folder-watch", "--disable-feature=companion"});
    expect(selected && !selected.value().organizer && !selected.value().watch && !selected.value().notifications
        && !selected.value().companion, "repeated options compose idempotently");
    const auto all = nookbit::parseFeatureSelection({"--disable-feature=all"});
    expect(all && !all.value().organizer && !all.value().watch && !all.value().notifications && !all.value().companion,
        "all disables every feature");
    for (const auto& invalid : {"--disable-feature", "--disable-feature=", "--disable-feature=typo"})
        expect(!nookbit::parseFeatureSelection({invalid}), "invalid feature options fail explicitly");
    return failures ? 1 : 0;
}

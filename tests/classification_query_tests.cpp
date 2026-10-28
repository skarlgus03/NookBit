#include "application/use_cases/classify_files_use_case.hpp"
#include "modules/rules/rule_engine.hpp"
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>

namespace {
int failures{};
// 분류 조회 계약의 예상 결과와 다르면 실패를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}

struct Rules final : nookbit::IRuleSetRepository {
    mutable int reads{};
    bool fail{false};
    nookbit::RuleSet value;
    // 메모리에 있는 규칙 복사본을 반환하고 읽기 횟수를 기록하는 함수.
    nookbit::Result<nookbit::RuleSet> load() const override {
        ++reads;
        if (fail) return nookbit::Result<nookbit::RuleSet>::failure({"test.rules", "Rules unavailable."});
        return nookbit::Result<nookbit::RuleSet>::success(value);
    }
};

struct RevisionHasher final : nookbit::IPlanHasher {
    mutable std::map<std::string, std::string> identifiers;
    mutable int fileReads{};
    bool invalid{false};
    // 입력 문자열의 변화만 구분하는 가짜 식별값을 반환하는 함수.
    std::string sha256(std::string_view bytes) const override {
        if (invalid) return "invalid";
        const std::string key(bytes);
        if (!identifiers.contains(key)) {
            std::ostringstream id; id << std::hex << std::setw(64) << std::setfill('0') << identifiers.size();
            identifiers[key] = id.str();
        }
        return identifiers.at(key);
    }
    // 분류 조회에서 금지한 파일 내용 읽기가 발생하면 오류로 반환하는 함수.
    nookbit::Result<std::string> sha256File(const std::filesystem::path&) const override {
        ++fileReads;
        return nookbit::Result<std::string>::failure({"test.file_io", "Classification must not hash file contents."});
    }
};

struct Engine final : nookbit::IRuleEngine {
    mutable int calls{};
    mutable std::int64_t time{};
    mutable nookbit::MultiMatchPolicy policy{};
    bool fail{false};
    std::function<void(int)> duringCall;
    // 호출 인자를 보관하고 기존 규칙 엔진으로 실제 판단하는 함수.
    nookbit::Result<std::optional<nookbit::RuleDecision>> decide(const nookbit::FileFact& file,
        const nookbit::RuleSet& rules, nookbit::MultiMatchPolicy requested, std::int64_t now) const override {
        ++calls; time = now; policy = requested;
        if (duringCall) duringCall(calls);
        if (fail) return nookbit::Result<std::optional<nookbit::RuleDecision>>::failure({"test.engine", "Classification failed."});
        return nookbit::RuleEngine{}.decide(file, rules, requested, now);
    }
};

// 분류 조회에 입력할 메타데이터만 만들며 실제 디스크는 읽지 않는 함수.
nookbit::FileFact fact(const wchar_t* name, bool directory = false) {
    nookbit::FileFact file;
    file.absolutePath = std::filesystem::path(L"C:/input") / name;
    file.relativePath = name; file.fingerprint.directory = directory;
    return file;
}

// 결과 순서·분류/제외/미분류·폴더 구분과 한 번 읽은 규칙의 일관성을 검사하는 함수.
void testQuery() {
    Rules rules; Engine engine; RevisionHasher hasher;
    rules.value.id = "query"; rules.value.version = "unchanged-version"; rules.value.schemaVersion = 3;
    nookbit::RuleNode photos; photos.id = "photos"; photos.atomicCondition = nookbit::ExtensionPredicate{{"jpg"}};
    photos.destination = nookbit::RuleDestination{L"Pictures", std::nullopt};
    nookbit::RuleNode excluded; excluded.id = "excluded"; excluded.atomicCondition = nookbit::ExtensionPredicate{{"tmp"}}; excluded.exclude = true;
    nookbit::RuleNode folders; folders.id = "folders"; folders.atomicCondition = nookbit::ItemTypePredicate{nookbit::ItemType::Folder};
    folders.destination = nookbit::RuleDestination{L"Folders", std::nullopt};
    rules.value.nodes = {photos, excluded, folders};
    const std::vector<nookbit::FileFact> input{fact(L"one.jpg"), fact(L"two.jpg"), fact(L"temp.tmp"), fact(L"note.txt"), fact(L"folder.jpg", true)};
    // 조회 중 저장소가 바뀌어도 현재 요청은 이미 읽은 규칙으로 끝내는지 재현한다.
    engine.duringCall = [&](int count) { if (count == 1) rules.value.nodes[0].destination->relativePath = L"NewPictures"; };
    nookbit::ClassifyFilesUseCase query(rules, engine, hasher);
    const auto result = query.classify(input, 123456);
    expect(result && result.value().files.size() == input.size() && rules.reads == 1, "one rule snapshot per complete batch");
    if (!result || result.value().files.size() != input.size()) return;
    for (std::size_t index = 0; index < input.size(); ++index)
        expect(result.value().files[index].source == input[index].absolutePath, "output preserves input path and order");
    const auto& decisions = result.value().files;
    expect(decisions[0].decision && decisions[1].decision && decisions[0].decision->destinationRelativePath == L"Pictures" &&
        decisions[1].decision->destinationRelativePath == L"Pictures", "repository mutation cannot mix rules inside one batch");
    expect(decisions[2].decision && decisions[2].decision->excluded && !decisions[3].decision && decisions[4].decision &&
        decisions[4].decision->destinationRelativePath == L"Folders", "excluded, unmatched and folder results stay distinct");
    expect(engine.time == 123456 && engine.policy == nookbit::MultiMatchPolicy::FirstMatch && hasher.fileReads == 0,
        "classification preserves caller time and first-match semantics without file-content IO");
    const auto changed = query.classify(input, 123457);
    expect(changed && changed.value().rulesRevision != result.value().rulesRevision && changed.value().files[0].decision &&
        changed.value().files[0].decision->destinationRelativePath == L"NewPictures", "content change invalidates revision even if version text is unchanged");
    const auto empty = query.classify({}, 0);
    expect(empty && empty.value().files.empty() && !empty.value().rulesRevision.empty(), "empty candidates still identify the active rules");
    rules.fail = true;
    const auto failedRules = query.classify(input, 0);
    expect(!failedRules && failedRules.error().code == "test.rules", "rule load failure propagates");
    rules.fail = false; engine.fail = true;
    const auto failedEngine = query.classify(input, 0);
    expect(!failedEngine && failedEngine.error().code == "test.engine", "engine error cannot become a successful partial batch");
    engine.fail = false; hasher.invalid = true;
    expect(!query.classify(input, 0), "unidentifiable rules cannot produce an apparently current result");
}
}

// 파일 시스템이나 UI 없이 분류 조회 계약을 검사하는 함수.
int main() { testQuery(); return failures ? 1 : 0; }

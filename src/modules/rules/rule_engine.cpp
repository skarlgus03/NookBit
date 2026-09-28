#include "modules/rules/rule_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>

namespace nookbit {
namespace {

// ASCII 글자를 대소문자 구분 없이 비교할 수 있게 변환하는 함수.
char lowerAscii(char character) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
}

// 확장자 앞의 점을 제거하고 대소문자를 통일하는 함수.
std::string normalizedExtension(std::string extension) {
    if (!extension.empty() && extension.front() == '.') extension.erase(extension.begin());
    std::transform(extension.begin(), extension.end(), extension.begin(), lowerAscii);
    return extension;
}

// 파일 경로를 UTF-8 바이트 문자열로 옮기는 함수.
std::string utf8Path(const std::filesystem::path& path) {
    const auto value = path.u8string();
    std::string result;
    result.reserve(value.size());
    for (const char8_t character : value) result.push_back(static_cast<char>(character));
    return result;
}

// 별표와 물음표를 지원하는 파일명 패턴을 비교하는 함수.
bool matchesGlob(std::string_view pattern, std::string_view fileName) {
    std::size_t patternIndex = 0;
    std::size_t nameIndex = 0;
    std::size_t lastStar = std::string_view::npos;
    std::size_t nameAfterStar = 0;
    while (nameIndex < fileName.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' ||
             lowerAscii(pattern[patternIndex]) == lowerAscii(fileName[nameIndex]))) {
            ++patternIndex;
            ++nameIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            lastStar = patternIndex++;
            nameAfterStar = nameIndex;
        } else if (lastStar != std::string_view::npos) {
            patternIndex = lastStar + 1;
            nameIndex = ++nameAfterStar;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') ++patternIndex;
    return patternIndex == pattern.size();
}

// 파일 정보가 규칙의 모든 조건 그룹을 만족하는지 확인하는 함수.
template<typename Condition>
bool matchesRule(const Condition& rule, const FileFact& file, std::int64_t nowUtcMs) {
    const auto& path = file.relativePath.empty() ? file.absolutePath : file.relativePath;
    std::string extension = file.extension.empty()
        ? utf8Path(path.extension()) : file.extension;
    extension = normalizedExtension(std::move(extension));
    if (!rule.extensions.empty()) {
        const bool found = std::any_of(rule.extensions.begin(), rule.extensions.end(),
            [&extension](const std::string& allowed) {
                return normalizedExtension(allowed) == extension;
            });
        if (!found) return false;
    }
    if (!rule.fileNameGlobs.empty()) {
        const auto fileName = utf8Path(path.filename());
        const bool found = std::any_of(rule.fileNameGlobs.begin(), rule.fileNameGlobs.end(),
            [&fileName](const std::string& glob) { return matchesGlob(glob, fileName); });
        if (!found) return false;
    }
    if (rule.minimumBytes && file.fingerprint.size < *rule.minimumBytes) return false;
    if (rule.maximumBytes && file.fingerprint.size > *rule.maximumBytes) return false;
    if constexpr (requires { rule.greaterThanBytes; }) {
        if (rule.greaterThanBytes && file.fingerprint.size <= *rule.greaterThanBytes) return false;
    }

    if (rule.minimumAgeDays || rule.maximumAgeDays) {
        constexpr long double millisecondsPerDay = 86400000.0L;
        const auto elapsedMs = static_cast<long double>(nowUtcMs) -
                               static_cast<long double>(file.fingerprint.modifiedUtcMs);
        const auto wholeDays = std::max(0.0L, std::floor(elapsedMs / millisecondsPerDay));
        if (rule.minimumAgeDays && wholeDays < *rule.minimumAgeDays) return false;
        if (rule.maximumAgeDays && wholeDays > *rule.maximumAgeDays) return false;
    }
    return true;
}

// 목적지 문자열 안의 날짜 토큰을 파일 수정일 기준으로 치환하는 함수.
void replaceToken(std::wstring& destination, const std::wstring& token,
                  const std::wstring& replacement) {
    std::size_t position = 0;
    while ((position = destination.find(token, position)) != std::wstring::npos) {
        destination.replace(position, token.size(), replacement);
        position += replacement.size();
    }
}

// 규칙 목적지를 펼치고 상위 경로 탈출 여부를 검사하는 함수.
Result<std::filesystem::path> destinationFor(const std::filesystem::path& relativePath,
                                              const FileFact& file) {
    using namespace std::chrono;
    const sys_time<milliseconds> modified{milliseconds{file.fingerprint.modifiedUtcMs}};
    const year_month_day date{floor<days>(modified)};
    if (!date.ok()) {
        return Result<std::filesystem::path>::failure({
            "rules.invalid_modified_time", "File modified time cannot be used for a destination.",
        });
    }
    auto destination = relativePath.wstring();
    auto year = std::to_wstring(static_cast<int>(date.year()));
    if (year.size() < 4) year.insert(0, 4 - year.size(), L'0');
    auto month = std::to_wstring(static_cast<unsigned>(date.month()));
    if (month.size() < 2) month.insert(0, 2 - month.size(), L'0');
    replaceToken(destination, L"{yyyy}", year);
    replaceToken(destination, L"{MM}", month);
    if (destination.find_first_of(L"{}") != std::wstring::npos) {
        return Result<std::filesystem::path>::failure({
            "rules.unknown_destination_token", "Destination contains an unsupported token.",
        });
    }
    const std::filesystem::path path{destination};
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return Result<std::filesystem::path>::failure({
            "rules.unsafe_destination", "Destination must be a relative path.",
        });
    }
    for (const auto& component : path) {
        if (component == L"..") {
            return Result<std::filesystem::path>::failure({
                "rules.unsafe_destination", "Destination cannot contain a parent path.",
            });
        }
    }
    return Result<std::filesystem::path>::success(path);
}

// 같은 높이에서 처음 맞는 일반 가지를 고르고, 없으면 else 가지를 고르는 함수.
const RuleNode* firstMatchingNode(const std::vector<RuleNode>& nodes, const FileFact& file,
                                  std::int64_t nowUtcMs) {
    const RuleNode* otherwise = nullptr;
    for (const auto& node : nodes) {
        if (!node.enabled) continue;
        if (node.otherwise) {
            otherwise = &node;
        } else if (matchesRule(node.condition, file, nowUtcMs)) {
            return &node;
        }
    }
    return otherwise;
}

// 선택한 리프의 목적지를 파일별 분류 결과로 변환하는 함수.
Result<std::optional<RuleDecision>> treeDecision(const FileFact& file, const RuleSet& rules,
                                                  std::int64_t nowUtcMs) {
    const RuleNode* node = firstMatchingNode(rules.nodes, file, nowUtcMs);
    while (node && !node->children.empty()) {
        node = firstMatchingNode(node->children, file, nowUtcMs);
    }
    const RuleDestination* target = node && node->destination ? &*node->destination : nullptr;
    std::string matchedId = node ? node->id : std::string();
    if (!target && rules.globalOtherwise) {
        target = &*rules.globalOtherwise;
        matchedId = "global-otherwise";
    }
    if (!target) return Result<std::optional<RuleDecision>>::success(std::nullopt);
    auto destination = destinationFor(target->relativePath, file);
    if (!destination) return Result<std::optional<RuleDecision>>::failure(destination.error());
    return Result<std::optional<RuleDecision>>::success(RuleDecision{
        matchedId, std::move(destination.value()),
        "Matched classification rule: " + matchedId, target->collisionPolicy,
    });
}

} // namespace

// 활성 규칙을 우선순위순으로 판단하고 선택된 목적지를 반환하는 함수.
Result<std::optional<RuleDecision>> RuleEngine::decide(
    const FileFact& file,
    const RuleSet& rules,
    MultiMatchPolicy multiMatchPolicy,
    std::int64_t nowUtcMs) const {
    if (rules.schemaVersion == 2) return treeDecision(file, rules, nowUtcMs);
    const ClassificationRule* selected = nullptr;
    for (const auto& rule : rules.rules) {
        if (!rule.enabled || !matchesRule(rule, file, nowUtcMs)) continue;
        if (selected && multiMatchPolicy == MultiMatchPolicy::Abort) {
            return Result<std::optional<RuleDecision>>::failure({
                "rules.multiple_matches", "More than one classification rule matches the file.",
            });
        }
        // 우선순위가 같으면 JSON에 먼저 나온 규칙을 유지한다.
        if (!selected || rule.priority > selected->priority) selected = &rule;
    }
    if (!selected) {
        return Result<std::optional<RuleDecision>>::success(std::nullopt);
    }
    auto destination = destinationFor(selected->destinationRelativePath, file);
    if (!destination) {
        return Result<std::optional<RuleDecision>>::failure(destination.error());
    }
    RuleDecision decision{
        selected->id,
        std::move(destination.value()),
        "Matched classification rule: " + selected->id,
        selected->collisionPolicy,
    };
    return Result<std::optional<RuleDecision>>::success(std::move(decision));
}

} // namespace nookbit

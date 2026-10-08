#include "presentation/character_controller.hpp"
#include <QThread>
#include <algorithm>
#include <utility>

namespace nookbit {
// 휴식 상태의 캐릭터 표시 모델을 만드는 생성자.
CharacterController::CharacterController(QObject* parent) : QObject(parent) {}
// 현재 캐릭터 상태를 반환하는 함수.
CharacterController::State CharacterController::state() const noexcept { return state_; }
// 그림·애니메이션 선택용 고정 상태 이름을 반환하는 함수.
QString CharacterController::stateKey() const {
    switch (state_) {
    case State::Resting: return QStringLiteral("resting");
    case State::Calling: return QStringLiteral("calling");
    case State::Working: return QStringLiteral("working");
    }
    return {};
}
// 현재 상태에 맞는 임시 말풍선 문구를 반환하는 함수.
QString CharacterController::message() const {
    return message_;
}
// 현재 선택된 상황 이름을 반환하는 함수.
QString CharacterController::cueId() const { return cueId_; }
// 현재 상황에 연결된 애니메이션 이름을 반환하는 함수.
QString CharacterController::animationId() const { return animationId_; }
// 향후 재생기가 참조할 검증된 스프라이트·애니메이션 설정을 반환하는 함수.
const CharacterPresentation& CharacterController::presentation() const noexcept { return presentation_; }
// 검증한 표현 설정을 교체하고 현재 상황을 다시 선택하는 함수.
Result<void> CharacterController::setPresentation(CharacterPresentation presentation) {
    if (QThread::currentThread() != thread())
        return Result<void>::failure({"character.wrong_thread", "Configure on the character's owning thread."});
    const auto checked = validateCharacterPresentation(presentation);
    if (!checked) return checked;
    presentation_ = std::move(presentation);
    refreshState();
    return Result<void>::success();
}
// 화면 스레드에서 기능별 활동·주의 요청을 받아 표시 상태를 갱신하는 함수.
Result<void> CharacterController::publish(const CharacterUpdate& update) {
    if (QThread::currentThread() != thread())
        return Result<void>::failure({"character.wrong_thread", "Publish on the character's owning thread."});
    if (update.sourceId.empty() || update.entryId.empty() ||
        update.sourceId.find('\0') != std::string::npos || update.entryId.find('\0') != std::string::npos ||
        update.cueId.find('\0') != std::string::npos)
        return Result<void>::failure({"character.invalid_id", "A source and entry identifier are required."});
    if (update.kind != CharacterUpdateKind::Activity && update.kind != CharacterUpdateKind::Attention)
        return Result<void>::failure({"character.unsupported_kind", "Unsupported character update kind."});
    auto& entries = update.kind == CharacterUpdateKind::Activity ? activities_ : attentions_;
    const EntryKey key{update.sourceId, update.entryId};
    if (update.active) entries.insert_or_assign(key, update);
    else entries.erase(key);
    refreshState();
    return Result<void>::success();
}
// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
void CharacterController::refreshState() {
    const auto next = !activities_.empty() ? State::Working : !attentions_.empty() ? State::Calling : State::Resting;
    auto text = QStringLiteral("잠깐 쉬고 있어요.");
    const auto fallback = next == State::Working ? "working" : next == State::Calling ? "calling" : "resting";
    std::string requestedCue = fallback;
    const CharacterUpdate* selected = nullptr;
    if (next != State::Resting) {
        const auto& entries = next == State::Working ? activities_ : attentions_;
        selected = &entries.begin()->second;
        if (!selected->cueId.empty()) requestedCue = selected->cueId;
        text = next == State::Working ? QStringLiteral("작업 중이에요.") : QStringLiteral("확인할 내용이 있어요!");
    }
    const auto findCue = [this](const std::string& id) {
        return std::find_if(presentation_.cues.begin(), presentation_.cues.end(),
            [&id](const CharacterCue& cue) { return cue.id == id; });
    };
    auto cue = findCue(requestedCue);
    if (cue == presentation_.cues.end()) { requestedCue = fallback; cue = findCue(fallback); }
    QString animation;
    if (cue != presentation_.cues.end()) {
        text = QString::fromStdString(cue->message);
        animation = QString::fromStdString(cue->animationId);
    }
    if (selected && !selected->message.empty()) text = QString::fromStdString(selected->message);
    const auto cueName = QString::fromStdString(requestedCue);
    if (next == state_ && text == message_ && cueName == cueId_ && animation == animationId_) return;
    state_ = next;
    message_ = text;
    cueId_ = cueName;
    animationId_ = animation;
    emit stateChanged();
}
} // namespace nookbit

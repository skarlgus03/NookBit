#include "presentation/character_controller.hpp"

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
    switch (state_) {
    case State::Resting: return QStringLiteral("잠깐 쉬고 있어요.");
    case State::Calling: return QStringLiteral("정리할 파일이 있어요!");
    case State::Working: return QStringLiteral("파일을 옮기고 있어요.");
    }
    return {};
}
// 파일 작업 여부를 전달받고 표시 상태를 갱신하는 함수.
void CharacterController::setWorking(bool working) {
    working_ = working;
    refreshState();
}
// 정리 제안 여부를 전달받고 표시 상태를 갱신하는 함수.
void CharacterController::setProposalAvailable(bool available) {
    proposalAvailable_ = available;
    refreshState();
}
// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
void CharacterController::refreshState() {
    const auto next = working_ ? State::Working : proposalAvailable_ ? State::Calling : State::Resting;
    if (next == state_) return;
    state_ = next;
    emit stateChanged();
}
} // namespace nookbit

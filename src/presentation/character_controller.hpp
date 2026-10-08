#pragma once
#include <QObject>
#include <QString>

namespace nookbit {
/// 다른 화면이나 파일 작업을 호출하지 않고 캐릭터 표시 상태만 관리한다.
class CharacterController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
public:
    enum class State { Resting, Calling, Working };
    Q_ENUM(State)
    /// 휴식 상태의 캐릭터 표시 모델을 만드는 생성자.
    explicit CharacterController(QObject* parent = nullptr);
    /// 현재 캐릭터 상태를 반환하는 함수.
    [[nodiscard]] State state() const noexcept;
    /// 그림·애니메이션 선택용 고정 상태 이름을 반환하는 함수.
    [[nodiscard]] QString stateKey() const;
    /// 현재 상태에 맞는 임시 말풍선 문구를 반환하는 함수.
    [[nodiscard]] QString message() const;
    /// 파일 작업 여부를 전달받고 표시 상태를 갱신하는 함수.
    void setWorking(bool working);
    /// 정리 제안 여부를 전달받고 표시 상태를 갱신하는 함수.
    void setProposalAvailable(bool available);
signals:
    void stateChanged();
private:
    /// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
    void refreshState();
    bool working_{false};
    bool proposalAvailable_{false};
    State state_{State::Resting};
};
} // namespace nookbit

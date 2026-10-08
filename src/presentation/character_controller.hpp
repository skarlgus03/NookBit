#pragma once
#include <QObject>
#include <QString>
#include <nookbit/application/character_channel.hpp>
#include <map>

namespace nookbit {
/// 다른 화면이나 파일 작업을 호출하지 않고 캐릭터 표시 상태만 관리한다.
class CharacterController final : public QObject, public ICharacterChannel {
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
    /// 화면 스레드에서 기능별 활동·주의 요청을 받아 표시 상태를 갱신하는 함수.
    [[nodiscard]] Result<void> publish(const CharacterUpdate& update) override;
signals:
    void stateChanged();
private:
    /// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
    void refreshState();
    using EntryKey = std::pair<std::string, std::string>;
    std::map<EntryKey, CharacterUpdate> activities_;
    std::map<EntryKey, CharacterUpdate> attentions_;
    QString message_{QStringLiteral("잠깐 쉬고 있어요.")};
    State state_{State::Resting};
};
} // namespace nookbit

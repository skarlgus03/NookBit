#pragma once
#include <QObject>
#include <QString>
#include <nookbit/application/character_channel.hpp>
#include <nookbit/domain/character_presentation.hpp>
#include <map>

namespace nookbit {
/// 다른 화면이나 파일 작업을 호출하지 않고 캐릭터 표시 상태만 관리한다.
class CharacterController final : public QObject, public ICharacterChannel {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateKey READ stateKey NOTIFY stateChanged)
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QString cueId READ cueId NOTIFY stateChanged)
    Q_PROPERTY(QString animationId READ animationId NOTIFY stateChanged)
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
    /// 현재 선택된 상황 이름을 반환하는 함수.
    [[nodiscard]] QString cueId() const;
    /// 현재 상황에 연결된 애니메이션 이름을 반환하는 함수.
    [[nodiscard]] QString animationId() const;
    /// 검증한 표현 설정을 교체하고 현재 상황을 다시 선택하는 함수.
    [[nodiscard]] Result<void> setPresentation(CharacterPresentation presentation);
    /// 향후 재생기가 참조할 검증된 스프라이트·애니메이션 설정을 반환하는 함수.
    [[nodiscard]] const CharacterPresentation& presentation() const noexcept;
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
    QString cueId_{QStringLiteral("resting")};
    QString animationId_;
    CharacterPresentation presentation_;
    State state_{State::Resting};
};
} // namespace nookbit

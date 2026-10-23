#pragma once
#include <QObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <nookbit/application/character_channel.hpp>
#include <nookbit/domain/character_presentation.hpp>
#include <nookbit/domain/image_pack.hpp>
#include <optional>
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
    Q_PROPERTY(QVariantMap animation READ animation NOTIFY animationChanged)
    Q_PROPERTY(QVariantMap bubble READ bubble NOTIFY animationChanged)
    Q_PROPERTY(QVariantList previewAnimations READ previewAnimations NOTIFY previewAnimationsChanged)
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
    /// 재생기에 이미지 주소·격자·프레임 순서·시간을 값으로 전달하는 함수.
    [[nodiscard]] QVariantMap animation() const;
    /// 실제 활동 상태를 바꾸지 않고 미리보기용 애니메이션 목록을 반환하는 함수.
    [[nodiscard]] QVariantList previewAnimations() const;
    /// 원본 팩의 참조와 범위에 맞는 개인 편집값인지 검사하는 함수.
    [[nodiscard]] Result<void> validateEdits(const QVariantMap& edits) const;
    /// 검증한 개인 편집값을 실제 재생 데이터에 적용하는 함수.
    [[nodiscard]] Result<void> setEdits(QVariantMap edits);
    /// 말풍선의 부착점·피벗·선택적 배경을 값으로 전달하는 함수.
    [[nodiscard]] QVariantMap bubble() const;
    /// 검증한 표현 설정을 교체하고 현재 상황을 다시 선택하는 함수.
    [[nodiscard]] Result<void> setPresentation(CharacterPresentation presentation, QUrl imageBaseUrl = {});
    /// 검증된 ImagePack의 프레임과 상황 연결을 재생 데이터로 적용하는 함수.
    [[nodiscard]] Result<void> setImagePack(ImagePack pack);
    /// 검증된 스프라이트·애니메이션 설정을 반환하는 함수.
    [[nodiscard]] const CharacterPresentation& presentation() const noexcept;
    /// 화면 스레드에서 기능별 활동·주의 요청을 받아 표시 상태를 갱신하는 함수.
    [[nodiscard]] Result<void> publish(const CharacterUpdate& update) override;
signals:
    void stateChanged();
    void animationChanged();
    void previewAnimationsChanged();
private:
    /// 지정한 애니메이션을 독립적인 재생 데이터로 변환하는 함수.
    [[nodiscard]] QVariantMap animationFor(const QString& id) const;
    /// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
    void refreshState();
    using EntryKey = std::pair<std::string, std::string>;
    std::map<EntryKey, CharacterUpdate> activities_;
    std::map<EntryKey, CharacterUpdate> attentions_;
    QString message_{QStringLiteral("잠깐 쉬고 있어요.")};
    QString cueId_{QStringLiteral("resting")};
    QString animationId_;
    CharacterPresentation presentation_;
    QUrl imageBaseUrl_;
    std::optional<ImagePack> imagePack_;
    QVariantMap edits_;
    State state_{State::Resting};
};
} // namespace nookbit

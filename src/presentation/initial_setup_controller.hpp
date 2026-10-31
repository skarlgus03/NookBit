#pragma once
#include <QObject>
#include <QSettings>
#include <QUrl>
#include <functional>
#include <utility>
#include <nookbit/domain/result.hpp>

namespace nookbit {
struct InitialSetupDefaults {
    bool organizerAvailable{false};
    bool companionAvailable{false};
    QString destinationRoot;
    QString ruleLabel;
    bool companionShown{false};
    bool existingConfiguration{false};
};
struct InitialSetupRequest {
    QString destinationRoot;
    QUrl rulesFile;
    bool companionShown{false};
};
/// 첫 실행 선택과 완료 표식만 소유하며 기능 적용은 주입된 창구에 맡긴다.
class InitialSetupController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool required READ required NOTIFY requiredChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewingChanged)
    Q_PROPERTY(bool organizerAvailable READ organizerAvailable NOTIFY defaultsChanged)
    Q_PROPERTY(bool companionAvailable READ companionAvailable NOTIFY defaultsChanged)
    Q_PROPERTY(QString destinationRoot READ destinationRoot NOTIFY defaultsChanged)
    Q_PROPERTY(QString ruleLabel READ ruleLabel NOTIFY defaultsChanged)
    Q_PROPERTY(bool companionShown READ companionShown NOTIFY defaultsChanged)
    Q_PROPERTY(bool existingConfiguration READ existingConfiguration NOTIFY defaultsChanged)
public:
    using Apply = std::function<Result<void>(const InitialSetupRequest&)>;
    using ReadDefaults = std::function<InitialSetupDefaults()>;
    /// 현재 설정과 적용 계약을 받고 자동 검사에서는 개인 저장소를 사용하지 않는 생성자.
    InitialSetupController(QSettings& settings, bool persist, InitialSetupDefaults defaults, Apply apply, QObject* parent = nullptr);
    /// 아직 명시적으로 시작하거나 건너뛰지 않았는지 반환하는 함수.
    bool required() const { return required_; }
    /// 마지막 입력 또는 저장 오류를 반환하는 함수.
    QString message() const { return message_; }
    /// 자동 안내가 아닌 사용자가 요청한 다시 보기인지 반환하는 함수.
    bool reviewing() const { return reviewing_; }
    /// 현재 기능 설정을 다시 읽는 호스트 창구를 지정하는 함수.
    void setDefaultsReader(ReadDefaults reader) { readDefaults_ = std::move(reader); }
    /// 완료 기록은 유지한 채 최신 설정으로 안내를 다시 여는 함수.
    Q_INVOKABLE bool reopen();
    /// 이번 실행에서 정리 설정을 받을 수 있는지 반환하는 함수.
    bool organizerAvailable() const { return defaults_.organizerAvailable; }
    /// 이번 실행에서 컴패니언 표시를 변경할 수 있는지 반환하는 함수.
    bool companionAvailable() const { return defaults_.companionAvailable; }
    /// 기존 또는 기본 정리 경로를 반환하는 함수.
    QString destinationRoot() const { return defaults_.destinationRoot; }
    /// 기존 또는 기본 규칙의 짧은 이름을 반환하는 함수.
    QString ruleLabel() const { return defaults_.ruleLabel; }
    /// 현재 컴패니언 표시 선택을 반환하는 함수.
    bool companionShown() const { return defaults_.companionShown; }
    /// 이전 사용자 설정이 있었는지 반환하는 함수.
    bool existingConfiguration() const { return defaults_.existingConfiguration; }
    /// 선택을 검증하고 기능 적용 성공 후에만 완료를 기록하는 함수.
    Q_INVOKABLE bool finish(const QString& destination, bool importRules, const QUrl& rulesFile, bool showCompanion);
    /// 기능 설정을 변경하지 않고 다음 실행부터 안내를 생략하는 함수.
    Q_INVOKABLE bool skip();
    /// 선택한 로컬 파일·폴더 주소를 입력용 경로로 변환하는 함수.
    Q_INVOKABLE QString folderPath(const QUrl& url) const;
    /// 폴더 선택창이 열릴 기존 상위 폴더의 주소를 반환하는 함수.
    Q_INVOKABLE QUrl folderUrl(const QString& path) const;
signals:
    void requiredChanged();
    void messageChanged();
    void defaultsChanged();
    void reviewingChanged();
private:
    /// 실패 이유를 표시하고 미완료 상태를 유지하는 함수.
    bool fail(const QString& message);
    /// 완료 또는 건너뛰기 표식을 저장한 뒤 화면 종료를 알리는 함수.
    bool markCompleted();
    QSettings& settings_;
    bool persist_, required_, applying_{false}, reviewing_{false};
    InitialSetupDefaults defaults_;
    Apply apply_;
    ReadDefaults readDefaults_;
    QString message_;
};
}

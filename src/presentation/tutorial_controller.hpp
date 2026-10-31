#pragma once
#include <QObject>
#include <QSettings>

namespace nookbit {
/// 기능 설정이나 작업 권한 없이 튜토리얼의 열림과 완료 기록만 소유하는 창구.
class TutorialController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool required READ required NOTIFY requiredChanged)
    Q_PROPERTY(bool reviewing READ reviewing NOTIFY reviewingChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
public:
    /// 완료 기록만 읽고 기능 설정이나 파일 작업 창구는 받지 않는 생성자.
    TutorialController(QSettings& settings, bool persist, QObject* parent = nullptr);
    /// 튜토리얼이 현재 열려 있어야 하는지 반환하는 함수.
    bool required() const { return required_; }
    /// 사용자가 설정에서 다시 연 안내인지 반환하는 함수.
    bool reviewing() const { return reviewing_; }
    /// 완료 기록 저장 실패 이유를 반환하는 함수.
    QString message() const { return message_; }
    /// 완료 기록을 유지하면서 사용법 안내를 다시 여는 함수.
    Q_INVOKABLE bool reopen();
    /// 마지막 안내를 마치고 완료 기록만 저장하는 함수.
    Q_INVOKABLE bool finish();
    /// 기능을 변경하지 않고 안내를 건너뛰거나 다시 보기를 닫는 함수.
    Q_INVOKABLE bool skip();
signals:
    void requiredChanged();
    void reviewingChanged();
    void messageChanged();
private:
    /// 최초 안내에서만 완료 표식을 쓰고 다시 보기는 저장 없이 닫는 함수.
    bool close();
    QSettings& settings_;
    bool persist_, required_, reviewing_{false};
    QString message_;
};
}

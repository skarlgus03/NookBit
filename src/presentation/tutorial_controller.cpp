#include "presentation/tutorial_controller.hpp"
#include <QThread>

namespace nookbit {
// 기존 처음 설정 완료 기록을 호환해 이미 완료한 사용자에게 안내를 다시 강제하지 않는 생성자.
TutorialController::TutorialController(QSettings& settings, bool persist, QObject* parent)
    : QObject(parent), settings_(settings), persist_(persist),
      required_(persist && !settings.value(QStringLiteral("tutorial/completed"),
          settings.value(QStringLiteral("onboarding/completed"), false)).toBool()) {}

// 화면 스레드에서 닫힌 안내만 다시 열고 개인 설정은 변경하지 않는 함수.
bool TutorialController::reopen() {
    if (QThread::currentThread() != thread() || required_) return false;
    reviewing_ = true; required_ = true; message_.clear();
    emit messageChanged(); emit reviewingChanged(); emit requiredChanged();
    return true;
}

// 안내 완료 여부 외에는 어떤 설정도 저장할 수 없는 종료 함수.
bool TutorialController::close() {
    if (QThread::currentThread() != thread() || !required_) return false;
    if (persist_ && !reviewing_) {
        settings_.setValue(QStringLiteral("tutorial/completed"), true);
        settings_.sync();
        if (settings_.status() != QSettings::NoError) {
            settings_.remove(QStringLiteral("tutorial/completed"));
            message_ = QStringLiteral("튜토리얼 완료 기록을 저장하지 못했어요. 설정 저장 권한을 확인해 주세요.");
            emit messageChanged();
            return false;
        }
    }
    required_ = false; reviewing_ = false; message_.clear();
    emit messageChanged(); emit reviewingChanged(); emit requiredChanged();
    return true;
}
// 전체 안내를 읽었다는 명시적 요청으로 안내를 닫는 함수.
bool TutorialController::finish() { return close(); }
// 안내를 끝까지 읽지 않아도 기능 설정 변경 없이 닫는 함수.
bool TutorialController::skip() { return close(); }
}

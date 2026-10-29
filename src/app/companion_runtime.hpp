#pragma once
#include <nookbit/application/ports.hpp>
#include <QString>
#include <memory>

class QSettings;
class QGuiApplication;
namespace nookbit {
class CharacterController;
class CharacterSettingsController;
class CharacterWindowController;
class CompanionEditController;
class CompanionInteractionController;
class CompanionMotionController;
class BackgroundTaskRunner;
class UiPageRegistry;

struct CompanionRuntimeConfig {
    QString appData;
    QString applicationDir;
    bool verifyUi{false};
};

/// 파일 정리와 독립된 팩·표현·입력·모션 및 개인 설정의 수명을 묶는 소유자.
/// 앱과 외부 설정은 이 묶음보다 오래, QML과 기능 간 연결은 짧게 살아 있어야 한다.
class CompanionRuntime final {
public:
    /// 기존 팩과 설정을 복원하고 내부 표현 연결을 구성하는 생성자.
    CompanionRuntime(CompanionRuntimeConfig config, QSettings& preferences, QGuiApplication& application);
    /// 팩 가져오기가 끝날 때까지 저장소를 유지하고 타이머·연결을 해제하는 소멸자.
    ~CompanionRuntime();
    /// 시작 표현과 기본 행동 구성이 성공했는지 반환하는 함수.
    const Result<void>& startupResult() const;
    /// 관리 페이지의 메뉴·경로·입력을 컴패니언 수명에 묶어 등록하는 함수.
    Result<void> registerUi(UiPageRegistry& registry);
    /// 외부 활동·알림을 표현하는 컴패니언 창구를 반환하는 함수.
    CharacterController& character();
    /// 팩 선택·배율·기준점 설정 창구를 반환하는 함수.
    CharacterSettingsController& settings();
    /// 독립 창 위치와 노출 창구를 반환하는 함수.
    CharacterWindowController& window();
    /// 개인 프레임 편집 창구를 반환하는 함수.
    CompanionEditController& edits();
    /// 클릭·드래그 등 입력 창구를 반환하는 함수.
    CompanionInteractionController& interactions();
    /// 자율 걷기 창구를 반환하는 함수.
    CompanionMotionController& motion();
    /// 파일 작업과 별개인 팩 가져오기 실행기를 반환하는 함수.
    BackgroundTaskRunner& imports();
private:
    struct State;
    std::unique_ptr<State> state_;
};
}

#pragma once
#include <nookbit/domain/result.hpp>
#include <QStringList>

namespace nookbit {
/// 사용자가 요청한 시작 기능이며 실제 활성화 결과와 구분한다.
struct FeatureSelection {
    bool organizer{true};
    bool watch{true};
    bool notifications{true};
    bool companion{true};
};
struct FeatureStartupPlan {
    FeatureSelection requested;
    FeatureSelection active;
    QStringList messages;
};
/// 꺼진 필수 의존성을 다시 켜지 않고 시작 가능한 기능과 이유를 계산하는 함수.
FeatureStartupPlan resolveFeatureStartup(FeatureSelection requested);
/// 저장된 설정을 바꾸지 않는 명시적인 시작 옵션을 읽고 알 수 없는 기능을 거부하는 함수.
Result<FeatureSelection> parseFeatureSelection(const QStringList& arguments);
}

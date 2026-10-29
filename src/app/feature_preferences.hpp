#pragma once
#include "app/feature_startup.hpp"

class QSettings;
namespace nookbit {
/// 기능별 사용자 데이터를 건드리지 않고 다음 실행의 선택만 보관하는 저장소.
class FeaturePreferences final {
public:
    /// 앱 설정 저장소의 수명을 빌리는 생성자.
    explicit FeaturePreferences(QSettings& settings) : settings_(settings) {}
    /// 저장값 전체를 검증하며 첫 실행에는 모든 기능을 켜는 함수.
    Result<FeatureSelection> load() const;
    /// 한 설정 키에 선택을 저장하고 실패하면 기존 값을 복원하는 함수.
    Result<void> save(FeatureSelection selection);
private:
    QSettings& settings_;
};
}

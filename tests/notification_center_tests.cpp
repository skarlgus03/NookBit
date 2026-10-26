#include "modules/notifications/notification_center.hpp"
#include <iostream>

namespace {
int failures = 0;
// 기대와 다른 알림 정책 결과를 기록하는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
}
// 표시 정책이 발생 사실과 분리되고 중복·해제·종류 참조가 일관적인지 검사하는 함수.
int main() {
    nookbit::NotificationCenter center;
    expect(center.registerType({"file.offer", "watch", "Watch", "Offer", "Folder offer", true}).hasValue(), "first type registers");
    expect(center.registerType({"other.reminder", "other", "Other", "Reminder", "Other feature", false}).hasValue(), "another source registers independently");
    expect(!center.registerType({"file.offer", "other", "Other", "Duplicate", "", false}), "duplicate ids cannot replace metadata or policy");
    expect(!center.registerType({"../bad", "other", "Other", "Bad", "", true}), "unsafe ids are rejected");
    expect(center.enabled("file.offer") && !center.enabled("other.reminder"), "default policies remain distinct");
    expect(!center.publish({"unknown", "one", true, "", ""}), "unknown types cannot publish");
    expect(!center.publish({"file.offer", "", true, "", ""}), "instances require stable ids");
    nookbit::NotificationUpdate first{"file.offer", "first", true, "Offer", "original"};
    expect(center.publish(first).hasValue() && center.publish(first).hasValue() && center.visible().size() == 1, "duplicates update one instance");
    first.message = "updated";
    expect(center.publish(first).hasValue() && center.visible().front().message == "updated", "new content replaces old content");
    expect(center.publish({"file.offer", "second", true, "Offer", "second"}).hasValue(), "two instances coexist");
    expect(center.setEnabled("file.offer", false).hasValue() && center.visible().empty(), "muting hides all instances of a type");
    first.active = false;
    expect(center.publish(first).hasValue(), "muted instances still resolve");
    expect(center.setEnabled("file.offer", true).hasValue() && center.visible().size() == 1 && center.visible().front().instanceId == "second",
        "reenabling restores only still-active instances");
    expect(center.publish({"other.reminder", "second", true, "Reminder", "other"}).hasValue() && center.visible().size() == 1,
        "another muted source does not leak through");
    expect(center.setEnabled("other.reminder", true).hasValue() && center.visible().size() == 2, "same instance id is independent across types");
    expect(!center.setEnabled("unknown", true), "unknown type policies cannot be created accidentally");
    expect(center.publish({"file.offer", "second", false, "", ""}).hasValue() && center.visible().size() == 1, "resolving one source preserves the other");
    expect(center.publish({"other.reminder", "second", false, "", ""}).hasValue() && center.visible().empty(), "all instances can resolve");
    return failures ? 1 : 0;
}

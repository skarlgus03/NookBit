#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QJSValue>
#include <QPointF>
#include <memory>
#include <iostream>

namespace {
int failures = 0;
// 편집 상태 계약이 달라지면 해당 동작을 설명하고 실패시키는 함수.
void expect(bool value, const char* message) {
    if (!value) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 화면이나 컨트롤러 없이 실제 편집 상태만 생성하는 함수.
std::unique_ptr<QObject> create(QQmlEngine& engine, const QVariantMap& inputs) {
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(NOOKBIT_SOURCE_DIR "/src/presentation/qml/CompanionEditState.qml")));
    auto result = std::unique_ptr<QObject>(component.createWithInitialProperties(inputs));
    if (!result) { ++failures; std::cerr << component.errorString().toStdString(); }
    return result;
}
// 실제 QML 편집 명령을 호출하고 스크립트 오류까지 검사하는 함수.
QJSValue call(QQmlEngine& engine, QObject* state, const char* method, const QVariantList& args = {}) {
    auto object = engine.newQObject(state);
    QJSValueList values;
    for (const auto& arg : args) values.append(engine.toScriptValue(arg));
    const auto result = object.property(QString::fromLatin1(method)).callWithInstance(object, values);
    expect(!result.isError(), method);
    return result;
}
// 편집 상태의 중첩된 JS 값을 비교 가능한 맵으로 읽는 함수.
QVariantMap map(QObject* object, const char* property) {
    return object->property(property).value<QJSValue>().toVariant().toMap();
}
}

// 개인 좌표·프레임 시간 초안이 원본과 다른 편집 세션을 바꾸지 않는지 검사하는 함수.
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QQmlEngine engine;
    QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& errors) {
        for (const auto& error : errors) { ++failures; std::cerr << error.toString().toStdString() << '\n'; }
    });
    const QVariantMap point{{"x", 10}, {"y", 20}};
    const QVariantMap first{{"frameId", "a"}, {"pivot", point}, {"durationMs", 100}};
    const QVariantMap second{{"frameId", "b"}, {"pivot", QVariantMap{{"x", 40}, {"y", 50}}}, {"durationMs", 200}};
    const QVariantList steps{first, second};
    const QVariantMap saved{{"frames", QVariantMap{{"a", QVariantMap{{"pivot", point}}},
        {"unrelated", QVariantMap{{"pivot", point}}}}}, {"durations", QVariantMap{{"idle", QVariantMap{{"1", 200}}}}}};
    const QVariantMap inputs{{"savedEdits", saved}, {"step", first}, {"steps", steps}, {"animationId", "idle"},
        {"editing", true}, {"frameReady", true}, {"userPivotOffset", QPointF(2, 3)}, {"userBubbleOffset", QPointF(4, 5)}};
    auto a = create(engine, inputs), b = create(engine, inputs);
    if (!a || !b) return 1;
    expect(!a->property("dirty").toBool(), "initial saved edits are clean");
    call(engine, a.get(), "setCoordinates", {30, 40, 50, 70});
    const auto edited = map(a.get(), "drafts").value("a").toMap();
    expect(edited.value("pivot").toMap() == QVariantMap{{"x", 28}, {"y", 37}}
        && edited.value("attachment").toMap() == QVariantMap{{"x", 18}, {"y", 28}},
        "absolute input preserves the requested pivot/contact after personal offsets");
    expect(a->property("dirty").toBool() && !b->property("dirty").toBool()
        && map(b.get(), "drafts").value("a").toMap().value("pivot").toMap() == point,
        "editing one session cannot mutate another session or the saved snapshot");
    call(engine, a.get(), "alignFrames", {2});
    const auto aligned = map(a.get(), "drafts");
    expect(aligned.value("a").toMap() == aligned.value("b").toMap() && aligned.contains("unrelated"),
        "alignment updates this animation and preserves unrelated frame edits");
    expect(call(engine, a.get(), "resizeFrame", {0, 700}).toBool(), "valid duration edit is accepted");
    const QVariantMap authored{{"id", "idle"}, {"steps", steps}};
    const auto playback = call(engine, a.get(), "withDurationDrafts", {authored, steps}).toVariant().toMap().value("steps").toList();
    expect(playback.first().toMap().value("durationMs").toInt() == 700
        && playback.last().toMap().value("durationMs").toInt() == 200
        && authored.value("steps").toList().first().toMap().value("durationMs").toInt() == 100,
        "duration overrides affect only selected occurrences and never the authored descriptor");
    expect(!call(engine, a.get(), "resizeFrame", {-1, 500}).toBool(), "invalid frame indices are ignored");
    call(engine, a.get(), "resizeFrame", {0, -500});
    expect(map(a.get(), "durationDrafts").value("idle").toMap().value("0").toInt() == 16,
        "minimum frame duration remains enforced");
    a->setProperty("frameReady", false);
    const auto before = map(a.get(), "drafts");
    call(engine, a.get(), "movePoint", {0, 999, 999});
    expect(map(a.get(), "drafts") == before, "a loading frame cannot receive point edits");
    call(engine, a.get(), "resetEdits");
    expect(map(a.get(), "drafts").isEmpty() && map(a.get(), "durationDrafts").isEmpty(), "reset clears both draft groups");
    call(engine, a.get(), "reloadEdits");
    expect(!a->property("dirty").toBool() && map(a.get(), "drafts") == saved.value("frames").toMap(),
        "cancel restores the original saved edits");
    return failures ? 1 : 0;
}

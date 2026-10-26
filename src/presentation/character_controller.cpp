#include "presentation/character_controller.hpp"
#include <QThread>
#include <algorithm>
#include <utility>
#include <cmath>

namespace nookbit {
// 캐릭터 이미지와 별개인 말풍선 설정을 화면 값으로 변환하는 함수.
QVariantMap CharacterController::bubble() const {
    const auto value = imagePack_ ? imagePack_->bubble : ImagePackBubble{};
    QVariantMap result{{"attachment", QVariantMap{{"x", value.attachment.x}, {"y", value.attachment.y}}},
        {"pivot", QVariantMap{{"x", value.pivot.x}, {"y", value.pivot.y}}}};
    QVariantMap skin{{"kind", QStringLiteral("default")}};
    if (value.image) {
        const auto& image = *value.image;
        // 글과 버튼의 논리 사각형을 화면 데이터로 변환하는 함수.
        const auto rect = [](const ImagePackRect& r) { return QVariantMap{{"x", r.x}, {"y", r.y}, {"width", r.width}, {"height", r.height}}; };
        skin = {{"kind", QStringLiteral("image")}, {"source", QUrl::fromLocalFile(QString::fromStdWString(image.imagePath.wstring()))},
            {"imageWidth", image.imageWidth}, {"imageHeight", image.imageHeight}, {"width", image.size.width}, {"height", image.size.height},
            {"textRect", rect(image.textRect)}, {"actionRect", rect(image.actionRect)}};
    }
    result.insert("skin", skin);
    return result;
}
// 휴식 상태의 캐릭터 표시 모델을 만드는 생성자.
CharacterController::CharacterController(QObject* parent) : QObject(parent) {
    (void)behaviors_.registerBehavior({"resting", "resting", 0, {}, {}});
    (void)behaviors_.registerBehavior({"calling", "calling", 10, {"attention"}, {}});
    (void)behaviors_.registerBehavior({"working", "working", 20, {"working"}, {}});
}
// 기본 동작을 조건 데이터로 확장하고 즉시 다시 선택하는 함수.
Result<void> CharacterController::registerBehavior(CompanionBehavior behavior) {
    if (QThread::currentThread() != thread()) return Result<void>::failure({"character.wrong_thread", "Register on the owning thread."});
    auto result = behaviors_.registerBehavior(std::move(behavior));
    if (result) refreshState();
    return result;
}
// 업무 상태 입력과 충돌하지 않는 외부 상황 태그를 받는 함수.
Result<void> CharacterController::publish(const BehaviorUpdate& update) {
    if (QThread::currentThread() != thread()) return Result<void>::failure({"character.wrong_thread", "Publish on the owning thread."});
    if (update.sourceId == "character-status") return Result<void>::failure({"behavior.reserved_source", "This source is reserved for activity adaptation."});
    auto result = behaviors_.publish(update);
    if (result) refreshState();
    return result;
}
// 반응이 끝나면 보관한 과거 상태 대신 최신 상황으로 돌아가는 함수.
void CharacterController::presentInteraction(const QString& instanceId, const QString& cueId, const QString& message) {
    if (QThread::currentThread() != thread()) return;
    if (reactionId_ == instanceId && reactionCue_ == cueId && reactionMessage_ == message) return;
    reactionId_ = instanceId;
    reactionCue_ = instanceId.isEmpty() ? QString() : cueId;
    reactionMessage_ = instanceId.isEmpty() ? QString() : message;
    refreshState();
    emit reactionChanged();
}
// 현재 캐릭터 상태를 반환하는 함수.
CharacterController::State CharacterController::state() const noexcept { return state_; }
// 그림·애니메이션 선택용 고정 상태 이름을 반환하는 함수.
QString CharacterController::stateKey() const {
    switch (state_) {
    case State::Resting: return QStringLiteral("resting");
    case State::Calling: return QStringLiteral("calling");
    case State::Working: return QStringLiteral("working");
    }
    return {};
}
// 현재 상태에 맞는 임시 말풍선 문구를 반환하는 함수.
QString CharacterController::message() const {
    return message_;
}
// 현재 선택된 상황 이름을 반환하는 함수.
QString CharacterController::cueId() const { return cueId_; }
// 현재 상황에 연결된 애니메이션 이름을 반환하는 함수.
QString CharacterController::animationId() const { return animationId_; }
// 재생기에 이미지 주소·격자·프레임 순서·시간을 값으로 전달하는 함수.
QVariantMap CharacterController::animation() const {
    auto result = animationFor(animationId_);
    auto steps = result.value("steps").toList();
    const auto frames = edits_.value("frames").toMap();
    const auto durations = edits_.value("durations").toMap().value(animationId_).toMap();
    for (qsizetype i = 0; i < steps.size(); ++i) {
        auto step = steps[i].toMap();
        const auto edit = frames.value(step.value("frameId").toString()).toMap();
        for (const auto& name : {QStringLiteral("pivot"), QStringLiteral("attachment")})
            if (edit.contains(name)) step.insert(name, edit.value(name));
        if (durations.contains(QString::number(i))) step.insert("durationMs", durations.value(QString::number(i)));
        steps[i] = step;
    }
    if (!steps.isEmpty()) result.insert("steps", steps);
    if (!result.isEmpty()) result.insert("cueId", cueId_);
    return result;
}
// 참조·좌표·시간을 모두 검사하여 개인 편집값을 검증하는 함수.
Result<void> CharacterController::validateEdits(const QVariantMap& edits) const {
    const auto fail = [] { return Result<void>::failure({"character.invalid_edits", "편집값의 프레임·애니메이션 참조 또는 좌표·지속시간을 확인해 주세요."}); };
    // JSON 숫자만 허용하고 문자열·불리언의 자동 변환을 차단하는 함수.
    const auto number = [](const QVariant& value) {
        const auto type = value.metaType().id();
        return type == QMetaType::Double || type == QMetaType::Float || type == QMetaType::Int ||
            type == QMetaType::UInt || type == QMetaType::LongLong || type == QMetaType::ULongLong;
    };
    for (auto it = edits.begin(); it != edits.end(); ++it)
        if ((it.key() != "frames" && it.key() != "durations") || it.value().metaType().id() != QMetaType::QVariantMap) return fail();
    const auto frames = edits.value("frames").toMap();
    const auto durations = edits.value("durations").toMap();
    if (!imagePack_) return frames.isEmpty() && durations.isEmpty() ? Result<void>::success() : fail();
    for (auto frame = frames.begin(); frame != frames.end(); ++frame) {
        if (!imagePack_->frames.contains(frame.key().toStdString()) || frame.value().metaType().id() != QMetaType::QVariantMap) return fail();
        const auto points = frame.value().toMap();
        for (auto point = points.begin(); point != points.end(); ++point) {
            if ((point.key() != "pivot" && point.key() != "attachment") || point.value().metaType().id() != QMetaType::QVariantMap) return fail();
            const auto xy = point.value().toMap();
            if (xy.size() != 2 || !xy.contains("x") || !xy.contains("y")) return fail();
            for (const auto& value : xy)
                if (!number(value) || !std::isfinite(value.toDouble()) || std::abs(value.toDouble()) > 1024) return fail();
        }
    }
    for (auto animation = durations.begin(); animation != durations.end(); ++animation) {
        const auto found = imagePack_->animations.find(animation.key().toStdString());
        if (found == imagePack_->animations.end() || animation.value().metaType().id() != QMetaType::QVariantMap) return fail();
        const auto values = animation.value().toMap();
        for (auto step = values.begin(); step != values.end(); ++step) {
            bool validIndex = false;
            const int index = step.key().toInt(&validIndex);
            const double ms = step.value().toDouble();
            if (!validIndex || step.key() != QString::number(index) || index < 0 || static_cast<size_t>(index) >= found->second.steps.size() ||
                !number(step.value()) || !std::isfinite(ms) || ms < 16 || ms > 60000 || std::floor(ms) != ms) return fail();
        }
    }
    return Result<void>::success();
}
// 원본 데이터를 보존하며 실제 재생에 개인 편집값을 반영하는 함수.
Result<void> CharacterController::setEdits(QVariantMap edits) {
    if (QThread::currentThread() != thread()) return Result<void>::failure({"character.wrong_thread", "Apply on the owning thread."});
    const auto checked = validateEdits(edits);
    if (!checked) return checked;
    if (edits_ != edits) { edits_ = std::move(edits); emit animationChanged(); }
    return Result<void>::success();
}
// 팩의 모든 애니메이션을 화면에서 독립적으로 선택할 수 있게 반환하는 함수.
QVariantList CharacterController::previewAnimations() const {
    QVariantList result;
    const auto append = [&](const QString& id) {
        result.append(QVariantMap{{"label", id}, {"descriptor", animationFor(id)}});
    };
    if (imagePack_) {
        for (const auto& [id, value] : imagePack_->animations) append(QString::fromStdString(id));
    } else {
        for (const auto& value : presentation_.animations) append(QString::fromStdString(value.id));
    }
    return result;
}
// 선택한 애니메이션의 프레임을 값으로 변환하며 실제 캐릭터 상태는 유지하는 함수.
QVariantMap CharacterController::animationFor(const QString& id) const {
    if (imagePack_) {
        const auto selected = imagePack_->animations.find(id.toStdString());
        if (selected == imagePack_->animations.end()) return {};
        QVariantList steps;
        for (const auto& step : selected->second.steps) {
            const auto& frame = imagePack_->frames.at(step.frameId);
            steps.append(QVariantMap{{"frameId", QString::fromStdString(step.frameId)},
                {"source", QUrl::fromLocalFile(QString::fromStdWString(frame.imagePath.wstring()))},
                {"imageWidth", frame.imageWidth}, {"imageHeight", frame.imageHeight},
                {"region", QVariantMap{{"x", frame.region.x}, {"y", frame.region.y},
                    {"width", frame.region.width}, {"height", frame.region.height}}},
                {"pivot", QVariantMap{{"x", frame.pivot.x}, {"y", frame.pivot.y}}},
                {"pixelsPerUnit", frame.pixelsPerUnit}, {"durationMs", step.durationMs}});
        }
        return {{"id", id}, {"steps", steps}, {"loop", selected->second.loop},
            {"filtering", imagePack_->filtering == ImagePackFiltering::Linear ? "linear" : "nearest"}};
    }
    const auto selected = std::find_if(presentation_.animations.begin(), presentation_.animations.end(),
        [&id](const CharacterAnimation& value) { return QString::fromStdString(value.id) == id; });
    if (selected == presentation_.animations.end() || imageBaseUrl_.isEmpty()) return {};
    const auto sheet = std::find_if(presentation_.sheets.begin(), presentation_.sheets.end(),
        [&selected](const CharacterSpriteSheet& value) { return value.id == selected->sheetId; });
    if (sheet == presentation_.sheets.end()) return {};
    QVariantList frames;
    for (const auto frame : selected->frames) frames.append(frame);
    return {{"id", id}, {"source", imageBaseUrl_.resolved(QUrl(QString::fromStdString(sheet->image)))},
        {"frameWidth", sheet->frameWidth}, {"frameHeight", sheet->frameHeight},
        {"columns", sheet->columns}, {"rows", sheet->rows}, {"frames", frames},
        {"frameDurationMs", selected->frameDurationMs}, {"loop", selected->loop}};
}
// 검증된 스프라이트·애니메이션 설정을 반환하는 함수.
const CharacterPresentation& CharacterController::presentation() const noexcept { return presentation_; }
// 검증한 표현 설정을 교체하고 현재 상황을 다시 선택하는 함수.
Result<void> CharacterController::setPresentation(CharacterPresentation presentation, QUrl imageBaseUrl) {
    if (QThread::currentThread() != thread())
        return Result<void>::failure({"character.wrong_thread", "Configure on the character's owning thread."});
    const auto checked = validateCharacterPresentation(presentation);
    if (!checked) return checked;
    const auto previousAnimationId = animationId_;
    const auto previousCueId = cueId_;
    presentation_ = std::move(presentation);
    imageBaseUrl_ = std::move(imageBaseUrl);
    imagePack_.reset();
    edits_.clear();
    refreshState();
    if (previousAnimationId == animationId_ && previousCueId == cueId_) emit animationChanged();
    emit previewAnimationsChanged();
    return Result<void>::success();
}
// 검증된 ImagePack의 프레임과 상황 연결을 재생 데이터로 적용하는 함수.
Result<void> CharacterController::setImagePack(ImagePack pack) {
    if (QThread::currentThread() != thread())
        return Result<void>::failure({"character.wrong_thread", "Configure on the character's owning thread."});
    const auto checked = validateImagePack(pack);
    if (!checked) return checked;
    const auto previousAnimation = animationId_;
    const auto previousCue = cueId_;
    imagePack_ = std::move(pack);
    edits_.clear();
    refreshState();
    if (previousAnimation == animationId_ && previousCue == cueId_) emit animationChanged();
    emit previewAnimationsChanged();
    return Result<void>::success();
}
// 화면 스레드에서 기능별 활동·주의 요청을 받아 표시 상태를 갱신하는 함수.
Result<void> CharacterController::publish(const CharacterUpdate& update) {
    if (QThread::currentThread() != thread())
        return Result<void>::failure({"character.wrong_thread", "Publish on the character's owning thread."});
    if (update.sourceId.empty() || update.entryId.empty() ||
        update.sourceId.find('\0') != std::string::npos || update.entryId.find('\0') != std::string::npos ||
        update.cueId.find('\0') != std::string::npos)
        return Result<void>::failure({"character.invalid_id", "A source and entry identifier are required."});
    if (update.kind != CharacterUpdateKind::Activity && update.kind != CharacterUpdateKind::Attention)
        return Result<void>::failure({"character.unsupported_kind", "Unsupported character update kind."});
    auto& entries = update.kind == CharacterUpdateKind::Activity ? activities_ : attentions_;
    const EntryKey key{update.sourceId, update.entryId};
    if (update.active) entries.insert_or_assign(key, update);
    else entries.erase(key);
    refreshState();
    return Result<void>::success();
}
// 작업 중을 우선하고 남은 제안 여부로 다음 상태를 고르는 함수.
void CharacterController::refreshState() {
    (void)behaviors_.publish({"character-status", "work", {"working"}, !activities_.empty()});
    (void)behaviors_.publish({"character-status", "attention", {"attention"}, !attentions_.empty()});
    const auto behavior = behaviors_.current();
    const auto behaviorName = QString::fromStdString(behavior ? behavior->id : "resting");
    const auto next = behaviorName == "working" ? State::Working : behaviorName == "calling" ? State::Calling : State::Resting;
    auto text = QStringLiteral("잠깐 쉬고 있어요.");
    const auto fallback = next == State::Working ? "working" : next == State::Calling ? "calling" : "resting";
    std::string requestedCue = behavior ? behavior->cueId : fallback;
    const CharacterUpdate* selected = nullptr;
    if (next != State::Resting) {
        const auto& entries = next == State::Working ? activities_ : attentions_;
        if (!entries.empty()) selected = &entries.begin()->second;
        if (selected && !selected->cueId.empty()) requestedCue = selected->cueId;
        text = next == State::Working ? QStringLiteral("작업 중이에요.") : QStringLiteral("확인할 내용이 있어요!");
    }
    if (!reactionId_.isEmpty()) requestedCue = reactionCue_.toStdString();
    const auto findCue = [this](const std::string& id) {
        return std::find_if(presentation_.cues.begin(), presentation_.cues.end(),
            [&id](const CharacterCue& cue) { return cue.id == id; });
    };
    auto cue = findCue(requestedCue);
    if (cue == presentation_.cues.end()) cue = findCue(fallback);
    QString animation;
    if (cue != presentation_.cues.end()) {
        text = QString::fromStdString(cue->message);
    }
    if (imagePack_) {
        auto binding = imagePack_->bindings.find(requestedCue);
        if (binding == imagePack_->bindings.end()) { requestedCue = fallback; binding = imagePack_->bindings.find(fallback); }
        animation = QString::fromStdString(binding->second);
    } else {
        if (findCue(requestedCue) == presentation_.cues.end()) requestedCue = fallback;
        if (cue != presentation_.cues.end()) animation = QString::fromStdString(cue->animationId);
    }
    if (selected && !selected->message.empty()) text = QString::fromStdString(selected->message);
    if (!reactionId_.isEmpty() && !reactionMessage_.isEmpty()) text = reactionMessage_;
    const auto cueName = QString::fromStdString(requestedCue);
    if (next == state_ && behaviorName == behaviorId_ && text == message_ && cueName == cueId_ && animation == animationId_) return;
    const bool animationReplaced = animationId_ != animation || cueId_ != cueName;
    state_ = next;
    behaviorId_ = behaviorName;
    message_ = text;
    cueId_ = cueName;
    animationId_ = animation;
    emit stateChanged();
    if (animationReplaced) emit animationChanged();
}
} // namespace nookbit

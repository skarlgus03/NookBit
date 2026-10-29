#include "app/companion_runtime.hpp"
#include "app/ui_page_registry.hpp"
#include "presentation/background_task_runner.hpp"
#include "presentation/character_controller.hpp"
#include "presentation/character_settings_controller.hpp"
#include "presentation/character_window_controller.hpp"
#include "presentation/companion_edit_controller.hpp"
#include "presentation/companion_interaction_controller.hpp"
#include "presentation/companion_motion_controller.hpp"
#include "infrastructure/configuration/json_character_presentation.hpp"
#include "infrastructure/configuration/json_image_pack_loader.hpp"
#include "infrastructure/configuration/directory_image_pack_library.hpp"
#include <QGuiApplication>
#include <QScreen>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <cmath>

namespace nookbit {
namespace {
// 실제 모니터의 사용 가능 영역만 값으로 수집하는 함수.
QList<QRect> characterScreens() {
    QList<QRect> areas;
    for (const auto* screen : QGuiApplication::screens()) areas.append(screen->availableGeometry());
    return areas;
}
// 첫 화면의 오른쪽 아래를 기본 위치로 정하는 함수.
QPoint initialAnchor() {
    const auto screens = characterScreens();
    return screens.isEmpty() ? QPoint(200, 200) : QPoint(screens.first().right() - 180, screens.first().bottom() - 140);
}
}

/// 팩 보관소는 가져오기 실행기보다 먼저 선언하여 작업 완료까지 살아 있는 내부 상태.
struct CompanionRuntime::State {
    QSettings& uiPreferences;
    QGuiApplication& application;
    const bool verifyUi;
    const QString appData;
    QMap<QString, QVariantMap> verificationEdits;
    CharacterController characterController;
    JsonImagePackLoader imagePackLoader;
    DirectoryImagePackLibrary imagePackLibrary;
    BackgroundTaskRunner characterImports;
    CharacterSettingsController characterSettings;
    CompanionEditController companionEdits;
    CharacterWindowController characterWindow;
    QTimer characterPositionSave;
    InteractionManager interactionManager;
    CompanionInteractionController companionInteractions;
    CompanionMotionController companionMotion;
    QObject connections;
    Result<void> result;

    // 외부 창구를 만들기 전에 모든 콜백이 참조할 소유 객체를 구성하는 생성자.
    State(CompanionRuntimeConfig config, QSettings& preferences, QGuiApplication& app)
        : uiPreferences(preferences), application(app), verifyUi(config.verifyUi), appData(std::move(config.appData)),
          imagePackLibrary(imagePackLoader,
              QDir(config.applicationDir).filePath(QStringLiteral("imagepacks/test/imagepack.json")),
              QDir(verifyUi ? config.applicationDir : appData).filePath(
                  verifyUi ? QStringLiteral("verify-unused-imagepacks") : QStringLiteral("imagepacks")),
              {{"builtin-nmoo", QDir(config.applicationDir).filePath(QStringLiteral("imagepacks/nmoo/imagepack.json"))}}),
          characterSettings(imagePackLibrary, [this](ImagePack pack) {
              // 값으로 받은 팩만 표현 창구에 적용하는 함수.
              return characterController.setImagePack(std::move(pack));
          }, characterImports),
          companionEdits(
              [this](const QVariantMap& edits) { return characterController.validateEdits(edits); },
              [this](const QVariantMap& edits) { return characterController.setEdits(edits); },
              [this](const QString& key) -> Result<QVariantMap> {
                  // 팩별 개인 편집 문서를 읽고 저장 형식을 검증하는 함수.
                  if (verifyUi) return Result<QVariantMap>::success(verificationEdits.value(key));
                  const auto bytes = uiPreferences.value(QStringLiteral("character/edits/") + key).toByteArray();
                  if (bytes.isEmpty()) return Result<QVariantMap>::success({});
                  const auto document = QJsonDocument::fromJson(bytes);
                  const auto root = document.object();
                  if (!document.isObject() || root.value("schemaVersion").toInt() != 1 || !root.value("edits").isObject())
                      return Result<QVariantMap>::failure({"character.invalid_saved_edits", "저장된 컴패니언 편집값을 읽지 못했어요. 팩 기본값을 사용해요."});
                  return Result<QVariantMap>::success(root.value("edits").toObject().toVariantMap());
              },
              [this](const QString& key, const QVariantMap& edits) -> Result<void> {
                  // 개인 편집값만 저장하고 저장 오류 시 이전 설정을 복구하는 함수.
                  if (verifyUi) { verificationEdits.insert(key, edits); return Result<void>::success(); }
                  const auto path = QStringLiteral("character/edits/") + key;
                  const bool existed = uiPreferences.contains(path);
                  const auto previous = uiPreferences.value(path);
                  const auto document = QJsonDocument::fromVariant(QVariantMap{{"schemaVersion", 1}, {"edits", edits}});
                  uiPreferences.setValue(path, document.toJson(QJsonDocument::Compact));
                  uiPreferences.sync();
                  if (uiPreferences.status() == QSettings::NoError) return Result<void>::success();
                  if (existed) uiPreferences.setValue(path, previous); else uiPreferences.remove(path);
                  uiPreferences.sync();
                  return Result<void>::failure({"character.save_edits_failed", "컴패니언 편집값을 저장하지 못했어요. 변경 내용은 초안에 남아 있어요."});
              }),
          characterWindow(characterScreens(), verifyUi ? initialAnchor()
              : uiPreferences.value(QStringLiteral("character/windowAnchor"), initialAnchor()).toPoint(),
              verifyUi || uiPreferences.value(QStringLiteral("character/windowShown"), true).toBool()),
          companionInteractions(interactionManager, "primary", [this] {
              // 입력 당시 기본 행동과 일시 반응을 값으로만 전달하는 함수.
              return InteractionContext{characterController.behaviorId().toStdString(), characterController.reactionId().toStdString()};
          }),
          companionMotion(
              [this] {
                  // 컴패니언의 현재 상황과 화면 범위만 모션 모듈에 값으로 전달하는 함수.
                  const auto behavior = characterController.behaviorId();
                  const auto area = characterWindow.screenArea();
                  const bool idleOrWalking = behavior == "resting" || behavior == "walking.left" || behavior == "walking.right";
                  return IdleWalkContext{characterWindow.shown() && characterController.canWalk() && idleOrWalking && characterController.reactionId().isEmpty(),
                      static_cast<double>(characterWindow.anchorX()), static_cast<double>(area.left() + 32), static_cast<double>(area.right() - 32)};
              },
              [this](const IdleWalkStep& step) {
                  // 모션 출력만 행동 창구와 창 위치에 연결하고 파일 작업에는 관여하지 않는 함수.
                  const auto accepted = static_cast<IBehaviorChannel&>(characterController).publish({"companion.motion", "idle-walk", {step.direction < 0 ? "walk.left" : "walk.right"}, step.walking});
                  if (!accepted) { QTextStream(stderr) << QString::fromStdString(accepted.error().message) << Qt::endl; return; }
                  if (step.move) characterWindow.moveAnchor(static_cast<int>(std::round(step.x)), characterWindow.anchorY());
              }, !verifyUi && uiPreferences.value(QStringLiteral("character/autonomousWalk"), true).toBool()),
          result(initialize()) {}

    // 기존 표현·팩·개인 보정·입력·모션을 복원하고 내부 연결을 만드는 함수.
    Result<void> initialize() {
        const auto bundledCharacter = QStringLiteral(":/nookbit/character.presentation.json");
        const auto characterSchema = QStringLiteral(":/nookbit/character-presentation.schema.json");
        const auto customCharacter = QDir(appData).filePath(QStringLiteral("character.presentation.json"));
        auto characterPath = !verifyUi && QFileInfo::exists(customCharacter) ? customCharacter : bundledCharacter;
        auto characterPresentation = loadCharacterPresentation(characterPath, characterSchema);
        if (!characterPresentation && !verifyUi) {
            QTextStream(stderr) << QString::fromStdString(characterPresentation.error().message) << Qt::endl;
            characterPath = bundledCharacter;
            characterPresentation = loadCharacterPresentation(bundledCharacter, characterSchema);
        }
        if (!characterPresentation) return Result<void>::failure(characterPresentation.error());
        const auto characterImageBase = characterPath.startsWith(QStringLiteral(":/"))
            ? QUrl(QStringLiteral("qrc") + QFileInfo(characterPath).path() + QStringLiteral("/"))
            : QUrl::fromLocalFile(QFileInfo(characterPath).absolutePath() + QStringLiteral("/"));
        const auto configuredCharacter = characterController.setPresentation(
            std::move(characterPresentation.value()), characterImageBase);
        if (!configuredCharacter) return configuredCharacter;
        characterSettings.refresh();
        const auto savedPack = verifyUi ? QString() : uiPreferences.value(QStringLiteral("character/selectedPack")).toString();
        if (!savedPack.isEmpty() && !characterSettings.selectPack(savedPack))
            QTextStream(stderr) << characterSettings.status() << Qt::endl;
        if (characterSettings.selectedKey().isEmpty() && (verifyUi || characterPath == bundledCharacter)) {
            if (!characterSettings.selectPack(QStringLiteral("builtin")) && verifyUi) return Result<void>::failure({"companion.startup_failed", "Cannot initialize companion presentation."});
        }
        if (!verifyUi) characterSettings.setUserScale(uiPreferences.value(
            QStringLiteral("character/scales/") + characterSettings.selectedKey(), 0.5).toDouble());
        // 선택한 팩과 개인 배율을 사용자 설정에서 복원하는 함수.
        QObject::connect(&characterSettings, &CharacterSettingsController::selected, &connections,
            [this](const QString& key) {
                if (verifyUi) return;
                characterSettings.setUserScale(uiPreferences.value(QStringLiteral("character/scales/") + key, 0.5).toDouble());
                uiPreferences.setValue(QStringLiteral("character/selectedPack"), key);
                uiPreferences.sync();
            });
        // 개인 배율 변경만 저장하고 팩 매니페스트는 수정하지 않는 함수.
        QObject::connect(&characterSettings, &CharacterSettingsController::scaleChanged, &connections,
            [this] {
                if (verifyUi || characterSettings.selectedKey().isEmpty()) return;
                uiPreferences.setValue(QStringLiteral("character/scales/") + characterSettings.selectedKey(), characterSettings.userScale());
                uiPreferences.sync();
            });
        // 팩별 개인 좌표를 복원하며 다른 팩의 보정값을 가져오지 않는 함수.
        const auto restoreCharacterPlacement = [this] {
            const auto prefix = QStringLiteral("character/placement/") + characterSettings.selectedKey();
            characterSettings.setPlacement(verifyUi ? QPointF() : uiPreferences.value(prefix + "/pivot", QPointF()).toPointF(),
                verifyUi ? QPointF() : uiPreferences.value(prefix + "/bubble", QPointF()).toPointF());
        };
        restoreCharacterPlacement();
        QObject::connect(&characterSettings, &CharacterSettingsController::selected, &connections,
            restoreCharacterPlacement);
        // 적용한 좌표만 개인 설정에 저장하고 팩 파일은 보존하는 함수.
        QObject::connect(&characterSettings, &CharacterSettingsController::placementChanged, &connections,
            [this] {
                if (verifyUi || characterSettings.selectedKey().isEmpty()) return;
                const auto prefix = QStringLiteral("character/placement/") + characterSettings.selectedKey();
                uiPreferences.setValue(prefix + "/pivot", characterSettings.pivotOffset());
                uiPreferences.setValue(prefix + "/bubble", characterSettings.bubbleOffset());
                uiPreferences.sync();
            });
        // 저장 위치는 조립부에서 제공하고 편집 담당자는 레지스트리를 직접 알지 않는다.
        companionEdits.loadPack(characterSettings.selectedKey());
        QObject::connect(&characterSettings, &CharacterSettingsController::selected, &companionEdits,
            &CompanionEditController::loadPack);
        // 독립 창 위치와 노출만 사용자 설정에 저장하는 함수.
        const auto saveCharacterWindow = [this] {
            if (verifyUi) return;
            uiPreferences.setValue(QStringLiteral("character/windowAnchor"), QPoint(characterWindow.anchorX(), characterWindow.anchorY()));
            uiPreferences.setValue(QStringLiteral("character/windowShown"), characterWindow.shown());
            uiPreferences.sync();
        };
        characterPositionSave.setSingleShot(true);
        characterPositionSave.setInterval(180);
        QObject::connect(&characterPositionSave, &QTimer::timeout, &connections, saveCharacterWindow);
        QObject::connect(&characterWindow, &CharacterWindowController::positionChanged, &characterPositionSave,
            [this] {
                // 드래그 중 좌표 변경은 즉시 반영하되 디스크 저장은 잠깐 묶는 함수.
                characterPositionSave.start();
            });
        QObject::connect(&characterWindow, &CharacterWindowController::visibilityChanged, &connections, saveCharacterWindow);
        QObject::connect(&application, &QCoreApplication::aboutToQuit, &connections, saveCharacterWindow);
        // 모니터 추가·제거와 작업 영역 변경 시 보이지 않는 위치를 복원하는 함수.
        const auto refreshCharacterScreens = [this] { characterWindow.setScreens(characterScreens()); };
        QObject::connect(&application, &QGuiApplication::screenRemoved, &characterWindow, refreshCharacterScreens);
        QObject::connect(&application, &QGuiApplication::screenAdded, &characterWindow,
            [this, refreshCharacterScreens](QScreen* screen) {
                // 새 화면의 작업 영역 변경도 같은 위치 복원 창구에 연결하는 함수.
                QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
                refreshCharacterScreens();
            });
        for (auto* screen : QGuiApplication::screens())
            QObject::connect(screen, &QScreen::availableGeometryChanged, &characterWindow, refreshCharacterScreens);
        const auto interactionsRegistered = registerBasicCompanionInteractions(interactionManager);
        if (!interactionsRegistered) return interactionsRegistered;
        QObject::connect(&companionInteractions, &CompanionInteractionController::reactionRequested,
            &characterController, &CharacterController::presentInteraction);
        const auto refreshInteractionTarget = [this] {
            // 바탕화면 표시 여부만 입력 가능 상태에 반영하는 함수.
            companionInteractions.setAvailable(characterWindow.shown());
        };
        QObject::connect(&characterWindow, &CharacterWindowController::visibilityChanged, &companionInteractions, refreshInteractionTarget);
        QObject::connect(&characterSettings, &CharacterSettingsController::selected, &companionInteractions,
            [this](const QString&) {
                // 새 팩에 이전 상호작용 반응을 이어서 표시하지 않는 함수.
                companionInteractions.cancel();
            });
        refreshInteractionTarget();
        for (const auto& behavior : {CompanionBehavior{"walking.left", "walk.left", 5, {"walk.left"}, {"working", "attention"}},
                                   CompanionBehavior{"walking.right", "walk.right", 5, {"walk.right"}, {"working", "attention"}}}) {
            if (!characterController.registerBehavior(behavior)) return Result<void>::failure({"companion.startup_failed", "Cannot initialize companion presentation."});
        }
        QObject::connect(&characterController, &CharacterController::stateChanged, &companionMotion, &CompanionMotionController::refresh);
        QObject::connect(&characterController, &CharacterController::reactionChanged, &companionMotion, &CompanionMotionController::refresh);
        QObject::connect(&characterController, &CharacterController::previewAnimationsChanged, &companionMotion, &CompanionMotionController::restart);
        QObject::connect(&characterWindow, &CharacterWindowController::visibilityChanged, &companionMotion, &CompanionMotionController::refresh);
        QObject::connect(&characterWindow, &CharacterWindowController::screenAreaChanged, &companionMotion, &CompanionMotionController::refresh);
        QObject::connect(&companionMotion, &CompanionMotionController::enabledChanged, &connections,
            [this] {
                // 사용자의 자율 걷기 선택만 자동 저장하는 함수.
                if (verifyUi) return;
                uiPreferences.setValue(QStringLiteral("character/autonomousWalk"), companionMotion.enabled());
                uiPreferences.sync();
            });
        companionMotion.refresh();
        return Result<void>::success();
    }
};

// 외부 앱·설정은 빌리고 컴패니언의 내부 객체는 함께 소유하는 생성자.
CompanionRuntime::CompanionRuntime(CompanionRuntimeConfig config, QSettings& preferences, QGuiApplication& application)
    : state_(std::make_unique<State>(std::move(config), preferences, application)) {}
// 종료된 연결과 타이머가 외부 앱으로 콜백을 남기지 않도록 해제하는 소멸자.
CompanionRuntime::~CompanionRuntime() = default;
// 시작 실패를 호스트가 표시하고 종료할 수 있도록 반환하는 함수.
const Result<void>& CompanionRuntime::startupResult() const { return state_->result; }
// 관리 페이지의 실제 입력은 이 기능이 선택하고 호스트에는 등록 정보만 전달하는 함수.
Result<void> CompanionRuntime::registerUi(UiPageRegistry& registry) {
    if (!startupResult()) return startupResult();
    const auto title = QCoreApplication::translate("CompanionUi", "컴패니언");
    return registry.addPage({QStringLiteral("companion.settings"), QStringLiteral("companion"), 5,
        title, QStringLiteral("companion"), title, 40,
        QUrl(QStringLiteral("qrc:/qt/qml/NookBit/presentation/qml/CompanionPage.qml")),
        {{"character", QVariant::fromValue(static_cast<QObject*>(&character()))},
         {"settings", QVariant::fromValue(static_cast<QObject*>(&settings()))},
         {"placement", QVariant::fromValue(static_cast<QObject*>(&window()))},
         {"motion", QVariant::fromValue(static_cast<QObject*>(&motion()))},
         {"edits", QVariant::fromValue(static_cast<QObject*>(&edits()))}}}, state_->connections);
}
// 소유한 characterController 창구를 빌려주는 함수.
CharacterController& CompanionRuntime::character() { return state_->characterController; }
// 소유한 characterSettings 창구를 빌려주는 함수.
CharacterSettingsController& CompanionRuntime::settings() { return state_->characterSettings; }
// 소유한 characterWindow 창구를 빌려주는 함수.
CharacterWindowController& CompanionRuntime::window() { return state_->characterWindow; }
// 소유한 companionEdits 창구를 빌려주는 함수.
CompanionEditController& CompanionRuntime::edits() { return state_->companionEdits; }
// 소유한 companionInteractions 창구를 빌려주는 함수.
CompanionInteractionController& CompanionRuntime::interactions() { return state_->companionInteractions; }
// 소유한 companionMotion 창구를 빌려주는 함수.
CompanionMotionController& CompanionRuntime::motion() { return state_->companionMotion; }
// 소유한 characterImports 창구를 빌려주는 함수.
BackgroundTaskRunner& CompanionRuntime::imports() { return state_->characterImports; }
}

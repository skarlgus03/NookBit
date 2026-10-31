#pragma once
#include <nookbit/domain/result.hpp>
#include <QObject>
#include <QMap>
#include <QSet>
#include <QUrl>
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>

namespace nookbit {
/// 등록 화면을 일반 확장 영역 또는 설정 상세 슬라이드에 배치하는 구분.
enum class UiPageHost { Extension, SettingsDetail };
/// 모듈이 제공하는 페이지의 메뉴 정보와 명시적인 화면 입력.
struct UiPageContribution {
    QString id;
    QString ownerId;
    int page{};
    QString title;
    QString icon;
    QString section;
    int order{};
    QUrl source;
    QVariantMap properties;
    bool inMenu{true};
    UiPageHost host{UiPageHost::Extension};
};
/// 기능 명령이나 화면 객체를 담지 않는 모듈 소유의 읽기 전용 안내 페이지.
struct UiTutorialStep {
    QString id;
    QString ownerId;
    QString title;
    QString summary;
    QStringList instructions;
    QString note;
    int order{};
};

/// 등록된 페이지 정보만 보관하며 기능 객체나 실제 QML 화면은 소유하지 않는 호스트 목록.
class UiPageRegistry final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(QVariantList extensionPages READ extensionPages NOTIFY pagesChanged)
    Q_PROPERTY(QVariantList tutorialSteps READ tutorialSteps NOTIFY tutorialStepsChanged)
public:
    /// 기존 호스트 페이지 번호와 충돌하지 않도록 예약 목록을 받는 생성자.
    explicit UiPageRegistry(QSet<int> reservedPages = {}, QObject* parent = nullptr);
    /// 수명 문맥보다 오래 사는 입력 창구만 빌리고 문맥 종료 시 등록을 제거하는 함수.
    /// 이 목록과 문맥은 화면 스레드에 있어야 하며 QML은 기능 소유자보다 먼저 해제해야 한다.
    Result<void> addPage(UiPageContribution page, QObject& lifetime);
    /// 기능의 여러 페이지를 모두 검증한 뒤 한 번에 등록하는 함수.
    Result<void> addPages(QList<UiPageContribution> pages, QObject& lifetime);
    /// 여러 안내를 검증 후 함께 등록하고 소유 모듈의 수명이 끝나면 제거하는 함수.
    Result<void> addTutorialSteps(QList<UiTutorialStep> steps, QObject& lifetime);
    /// 모듈을 알 필요 없이 순서와 안정적인 이름으로 정렬된 안내 복사본을 반환하는 함수.
    QVariantList tutorialSteps() const;
    /// 메뉴 순서대로 정렬한 페이지 값 복사본을 반환하는 함수.
    QVariantList pages() const;
    /// 별도 설정 슬라이드와 중복 생성하지 않을 일반 확장 페이지만 반환하는 함수.
    QVariantList extensionPages() const;
    /// 등록된 페이지 번호인지 확인하는 함수.
    Q_INVOKABLE bool contains(int page) const;
    /// 페이지의 제목·경로·입력 정보를 값으로 반환하는 함수.
    Q_INVOKABLE QVariantMap page(int page) const;
    /// 안정적인 페이지 이름을 기존 창 크기 설정과 호환되는 번호로 변환하는 함수.
    Q_INVOKABLE int pageId(const QString& id) const;
signals:
    void pagesChanged();
    void tutorialStepsChanged();
private:
    QSet<int> reservedPages_;
    QMap<int, UiPageContribution> pages_;
    QMap<QString, UiTutorialStep> tutorialSteps_;
};
}

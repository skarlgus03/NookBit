#pragma once

#include <nookbit/application/ports.hpp>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace nookbit {

/// 저장 전 규칙 초안을 보관하고 트리 편집을 화면에 제공하는 컨트롤러.
class RuleEditorController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString globalDestination READ globalDestination NOTIFY changed)
    Q_PROPERTY(bool legacyRules READ legacyRules NOTIFY changed)

public:
    /// 규칙 읽기·쓰기 계약을 받아 저장본을 초안으로 불러오는 생성자.
    RuleEditorController(IRuleSetRepository& reader, IRuleSetWriter& writer,
                         QObject* parent = nullptr);
    /// 이전 규칙 변환 계약까지 받아 편집기를 시작하는 생성자.
    RuleEditorController(IRuleSetRepository& reader, IRuleSetWriter& writer,
                         QObject* parent, IRuleSetMigrator* migrator,
                         QString graphLayoutPath = {});

    /// 화면에 표시할 깊이 우선 규칙 목록을 반환하는 함수.
    [[nodiscard]] QVariantList nodes() const;
    /// 최근 편집 또는 저장 상태 문구를 반환하는 함수.
    [[nodiscard]] QString message() const;
    /// 저장하지 않은 변경이 있는지 반환하는 함수.
    [[nodiscard]] bool dirty() const noexcept;
    /// 어느 가지도 목적지에 닿지 않을 때 사용할 전체 예외 목적지를 반환하는 함수.
    [[nodiscard]] QString globalDestination() const;
    /// 이전 v2 규칙을 사용 중인지 반환하는 함수.
    [[nodiscard]] bool legacyRules() const noexcept;

    /// 경로로 선택한 규칙의 상세 값을 반환하는 함수.
    Q_INVOKABLE QVariantMap detail(const QString& path) const;
    /// 선택한 규칙의 조건과 목적지를 초안에서 수정하는 함수.
    Q_INVOKABLE bool edit(const QString& path, const QString& id, bool enabled,
                          const QString& type, const QString& comparison,
                          const QString& value, const QString& sizeUnit,
                          const QString& destination);
    /// 루트 또는 선택한 부모 아래에 새 조건 규칙을 추가하는 함수.
    Q_INVOKABLE bool add(const QString& parentPath);
    /// 선택한 형제 목록의 마지막에 예외 규칙을 추가하는 함수.
    Q_INVOKABLE bool addOtherwise(const QString& parentPath);
    /// 선택한 규칙을 같은 높이에서 위나 아래로 옮기는 함수.
    Q_INVOKABLE bool move(const QString& path, int direction);
    /// 선택한 규칙과 그 자식을 초안에서 제거하는 함수.
    Q_INVOKABLE bool remove(const QString& path);
    /// 노드 화면의 연결과 입력을 검사한 뒤 편집 초안으로 변환하는 함수.
    Q_INVOKABLE bool importGraph(const QVariantList& blocks, const QVariantList& edges);
    /// 현재 규칙 초안을 노드와 연결선으로 펼쳐 반환하는 함수.
    Q_INVOKABLE QVariantMap graph() const;
    /// 현재 노드 배치를 분류 규칙과 별도 파일에 저장하는 함수.
    Q_INVOKABLE bool saveGraphLayout(const QVariantList& blocks, const QVariantList& edges);
    /// 저장하지 않은 수정을 버리고 저장본을 다시 불러오는 함수.
    Q_INVOKABLE void reload();
    /// 유효한 초안을 저장하여 실제 분류 규칙으로 적용하는 함수.
    Q_INVOKABLE bool save();
    /// 모든 규칙의 마지막 예외 목적지를 초안에서 변경하는 함수.
    Q_INVOKABLE void setGlobalDestination(const QString& destination);
    /// 사용자가 요청하면 백업 후 안전한 이전 규칙만 새 형식으로 변환하는 함수.
    Q_INVOKABLE bool migrateLegacyRules();

signals:
    void changed();
    void rulesSaved();

private:
    IRuleSetRepository& reader_;
    IRuleSetWriter& writer_;
    IRuleSetMigrator* migrator_;
    QString graphLayoutPath_;
    RuleSet draft_;
    QString message_;
    bool loaded_{false};
    bool legacyRules_{false};
    bool dirty_{false};
};

} // namespace nookbit

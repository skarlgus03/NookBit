#include "infrastructure/persistence_sqlite/sqlite_history_repository.hpp"
#include "presentation/history_controller.hpp"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QTimeZone>
#include <QSet>
#include <QVariantMap>
#include <iostream>

namespace {
int failures{};
// 기록 조회 결과가 기대와 다르면 원인을 남기는 함수.
void expect(bool condition, const char* message) {
    if (!condition) { ++failures; std::cerr << "FAIL: " << message << '\n'; }
}
// 파일을 건드리지 않고 실행 요청 여부와 되돌림 범위만 기록하는 대역.
class Operations final : public nookbit::IFileOperations {
public:
    int executions{}, rollbacks{};
    nookbit::PlanId rolledBack;
    // 검색 중 실행 계약이 잘못 호출되는지 기록하는 함수.
    nookbit::Result<void> executeApproved(const nookbit::OperationPlan&, const nookbit::ApprovalRecord&,
        const std::vector<std::filesystem::path>&, const nookbit::UserSettings&) override {
        ++executions; return nookbit::Result<void>::success();
    }
    // 사용자가 선택한 전체 작업 ID가 그대로 전달되는지 기록하는 함수.
    nookbit::Result<void> rollback(const nookbit::PlanId& id) override {
        ++rollbacks; rolledBack = id; return nookbit::Result<void>::success();
    }
};
// 읽기 실패를 재현하면서 직전 조회 조건을 검사할 수 있는 창구.
class QueryProbe final : public nookbit::IHistoryQuery {
public:
    mutable nookbit::HistorySearch last;
    bool fail{};
    // 요청을 기록하고 필요할 때만 조회 실패를 돌려주는 함수.
    nookbit::Result<nookbit::HistorySearchPage> searchHistory(const nookbit::HistorySearch& search) const override {
        last = search;
        if (fail) return nookbit::Result<nookbit::HistorySearchPage>::failure({"test.query", "query failed"});
        return nookbit::Result<nookbit::HistorySearchPage>::success({});
    }
};
// 유효한 저장 형식과 여러 상태·동일 시각을 가진 작은 기록들을 DB에만 만드는 함수.
bool seed(nookbit::SqliteHistoryRepository& repository, QSqlDatabase& writer) {
    nookbit::OperationPlan prototype;
    prototype.id = "template"; prototype.status = nookbit::PlanStatus::Planned;
    prototype.planHash = prototype.settingsRevision = prototype.rulesRevision = std::string(64, 'a');
    prototype.ruleSetVersion = "1";
    nookbit::MoveStep step;
    step.id = "template-step"; step.source = L"C:/Inbox/template.txt"; step.destination = L"D:/Archive/template.txt";
    prototype.steps.push_back(step);
    if (!repository.savePlan(prototype) || !writer.open()) return false;
    QSqlQuery fingerprint(writer);
    if (!fingerprint.exec("SELECT fingerprint_json FROM steps WHERE id='template-step'") || !fingerprint.next()) return false;
    const auto fingerprintJson = fingerprint.value(0);
    if (!writer.transaction()) return false;
    const QStringList statuses{"completed", "rolled_back", "failed", "executing", "rolling_back", "needs_recovery"};
    for (int index = 0; index < 120; ++index) {
        const auto id = QStringLiteral("p%1").arg(index, 3, 10, QChar('0'));
        const auto date = QDate(2026, 10, 1).startOfDay(QTimeZone::UTC).addSecs((index / 3) * 60).toString(Qt::ISODateWithMs);
        QSqlQuery plan(writer);
        plan.prepare("INSERT INTO plans(id,created_at,rule_set_version,settings_revision,rules_revision,plan_hash,status) VALUES(?,?,'1',?,?,?,?)");
        plan.addBindValue(id); plan.addBindValue(date);
        for (int part=0; part<3; ++part) plan.addBindValue(QString(64,'a'));
        plan.addBindValue(statuses[index % statuses.size()]);
        if (!plan.exec()) return false;
        for (int item = 0; item < (index == 0 ? 2 : 1); ++item) {
            auto name = QStringLiteral("commonfile-%1.txt").arg(index,3,10,QChar('0'));
            if (index == 5) name = "unique-needle.txt";
            if (index == 7) name = "Report_100%.txt";
            if (index == 8) name = "Report_1000.txt";
            if (item == 1) name = "unrelated.png";
            QSqlQuery file(writer);
            file.prepare("INSERT INTO steps(id,plan_id,sequence_no,source_path,destination_path,fingerprint_json,matched_rule_id,reason,collision_policy,status) VALUES(?,?,?,?,?,?,'rule','test','skip',?)");
            file.addBindValue(id + "/" + QString::number(item)); file.addBindValue(id); file.addBindValue(item);
            file.addBindValue("C:\\Inbox\\" + name);
            file.addBindValue(index == 6 ? QString::fromUtf8("D:\\Archive\\항목\\Meeting.PDF") : "D:\\Archive\\" + name);
            file.addBindValue(fingerprintJson);
            file.addBindValue(index % 6 == 0 ? "applied" : index % 6 == 1 ? "rolled_back" : "failed");
            if (!file.exec()) return false;
        }
    }
    QSqlQuery event(writer);
    if (!event.exec("INSERT INTO events(plan_id,occurred_at,event_type,payload_json) VALUES('p000','2026-10-10T00:00:00.000Z','test','{}')")) return false;
    return writer.commit();
}
// 전체 검색·리터럴 문자·기간·동일 시각 이어 읽기와 정렬 방향을 검사하는 함수.
void testQuery(nookbit::SqliteHistoryRepository& repository) {
    nookbit::HistorySearch query;
    auto first = repository.searchHistory(query);
    expect(first && first.value().entries.size() == 50 && first.value().next && first.value().entries.front().plan.id == "p000", "first page and last event date");
    for (auto order : {nookbit::HistoryOrder::Newest, nookbit::HistoryOrder::Oldest}) {
        query = {}; query.order = order;
        QSet<QString> ids;
        int pages{};
        do {
            const auto page = repository.searchHistory(query);
            if (!page) { expect(false, "read page"); break; }
            for (const auto& entry : page.value().entries) {
                const auto id = QString::fromStdString(entry.plan.id);
                expect(!ids.contains(id), "equal-time cursor has no duplicate plans"); ids.insert(id);
            }
            query.after = page.value().next;
            ++pages;
        } while (query.after && pages < 10);
        expect(ids.size() == 120 && pages == 3, "all pages include every executed plan and omit unsent plans");
    }
    query = {}; query.text = "UNIQUE-NEEDLE";
    auto found = repository.searchHistory(query);
    expect(found && found.value().entries.size() == 1 && found.value().entries[0].plan.id == "p005", "search reaches beyond latest 50 and ignores English case");
    for (const std::string text : {"100%", "Report_100%", "D:/Archive/항목", "d:\\archive\\항목", "Meeting.PDF"}) {
        query = {}; query.text = text;
        found = repository.searchHistory(query);
        expect(found && found.value().entries.size() == 1, "literal wildcards, Unicode and either path separator");
    }
    query = {}; query.text = "' OR 1=1 --";
    found = repository.searchHistory(query);
    expect(found && found.value().entries.empty(), "search text is a bound literal, never SQL");
    query = {}; query.text = "Inbox"; query.limit = 100;
    found = repository.searchHistory(query);
    expect(found && found.value().entries[0].matchedFileCount == 2 && found.value().entries[0].plan.fileCount == 2,
        "two matching steps produce one plan with a match count");
    for (auto filter : {nookbit::HistoryResultFilter::Completed, nookbit::HistoryResultFilter::RolledBack, nookbit::HistoryResultFilter::Problem}) {
        query = {}; query.result = filter; query.limit = 100;
        found = repository.searchHistory(query);
        expect(found && found.value().entries.size() == (filter == nookbit::HistoryResultFilter::Problem ? 80 : 20), "result categories use plan state");
    }
    query = {}; query.fromUtc = "2026-10-10T00:00:00.000Z"; query.untilUtc = "2026-10-11T00:00:00.000Z";
    found = repository.searchHistory(query);
    expect(found && found.value().entries.size() == 1 && found.value().entries[0].plan.id == "p000", "date filters use displayed last activity with inclusive start");
    query.fromUtc = "2026-10-01T00:00:00.000Z"; query.untilUtc = "2026-10-10T00:00:00.000Z"; query.limit = 100;
    found = repository.searchHistory(query);
    expect(found && found.value().entries.size() == 100 && found.value().entries[0].plan.id != "p000", "end time is exclusive");
    for (int limit : {0,101}) { query = {}; query.limit = limit; expect(!repository.searchHistory(query), "invalid page size rejected"); }
    query = {}; query.fromUtc = "invalid"; expect(!repository.searchHistory(query), "invalid date rejected");
    query = {}; query.after = nookbit::HistoryCursor{"invalid", "p000"}; expect(!repository.searchHistory(query), "invalid cursor rejected");
}
// 검색 후에도 전체 작업의 되돌리기 범위와 선택 보존이 유지되는지 검사하는 함수.
void testController(nookbit::SqliteHistoryRepository& repository) {
    Operations operations;
    nookbit::HistoryController controller(repository, repository, operations);
    controller.refreshHistory();
    controller.selectHistoryPlan("p000");
    controller.loadMoreHistory();
    expect(controller.historyEntries().size() == 100 && controller.selectedHistoryPlanId() == "p000", "load more preserves selected detail");
    expect(controller.applyHistoryFilters("commonfile-000.txt", "all", "completed", "newest", "", ""), "apply combined filters");
    controller.selectHistoryPlan("p000");
    expect(controller.historySteps().size() == 2 && controller.selectedHistoryFileCount() == 2
        && controller.historySteps()[0].toMap().value("searchMatch").toBool() && !controller.historySteps()[1].toMap().value("searchMatch").toBool(),
        "only matching step highlighted while full undo scope remains");
    expect(!controller.applyHistoryFilters("other", "custom", "all", "newest", "2026-10-12", "2026-10-01")
        && controller.selectedHistoryPlanId() == "p000" && controller.searchText() == "commonfile-000.txt", "invalid range preserves valid result and selected work");
    expect(operations.executions == 0 && operations.rollbacks == 0, "search and selection never execute file operations");
    controller.undoSelectedHistory();
    expect(operations.rollbacks == 1 && operations.rolledBack == "p000", "explicit undo forwards the entire selected plan");
    QueryProbe probe;
    nookbit::HistoryController dates(repository, probe, operations);
    dates.applyHistoryFilters("", "custom", "all", "oldest", "2026-10-01", "2026-10-01");
    const auto start = QDate(2026,10,1).startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs).toStdString();
    const auto end = QDate(2026,10,2).startOfDay(QTimeZone::systemTimeZone()).toUTC().toString(Qt::ISODateWithMs).toStdString();
    expect(probe.last.fromUtc == start && probe.last.untilUtc == end, "local inclusive dates become UTC day boundaries");
    probe.fail = true;
    expect(!dates.applyHistoryFilters("changed", "all", "all", "newest", "", "") && dates.period() == "custom" && !dates.searchMessage().isEmpty(), "query failure cannot replace applied filters");
}
}
// 사용자 파일 대신 임시 SQLite 기록으로 검색과 화면 창구를 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) return 1;
    const auto path = directory.filePath("history.sqlite");
    nookbit::SqliteHistoryRepository repository(path, QStringLiteral(NOOKBIT_SOURCE_DIR "/src/infrastructure/persistence_sqlite/migrations/001_initial.sql"));
    auto writer = QSqlDatabase::addDatabase("QSQLITE", "history-search-fixture"); writer.setDatabaseName(path);
    if (!seed(repository, writer)) { std::cerr << "fixture failed: " << writer.lastError().text().toStdString(); return 1; }
    testQuery(repository); testController(repository);
    writer.close();
    return failures ? 1 : 0;
}

#include "infrastructure/configuration/json_rule_set_repository.hpp"
#include "infrastructure/filesystem/document_text_terms.hpp"
#include "modules/rules/rule_engine.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

struct Document {
    QString fileName;
    QString expected;
    QString body;
    nookbit::FileFact fact;
};

using nookbit::document_recommendation::Profile;
using nookbit::document_recommendation::countTextTerms;
using nookbit::document_recommendation::prepareProfiles;
using nookbit::document_recommendation::recommend;

struct Prediction {
    QStringList ranked;
    std::optional<double> bestScore;
};

using Predict = std::function<Prediction(const Document&)>;

// 파일 내용을 수정하지 않고 바이트를 읽는 함수.
QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error(path.toStdString() + ": cannot read");
    auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) throw std::runtime_error(path.toStdString() + ": read failed");
    return bytes;
}

// UTF-8 텍스트나 내장 샘플을 평가 문서와 규칙 입력으로 변환하는 함수.
Document readDocument(const QJsonObject& row, const QFileInfo& manifest) {
    const auto path = row.value("path").toString();
    const auto expected = row.value("expected");
    if (path.isEmpty() || !(expected.isNull() || expected.isString()))
        throw std::runtime_error("Each document needs a path and an expected folder or null.");
    const QDir directory(manifest.absolutePath());
    const QFileInfo input(directory.absoluteFilePath(path));
    const auto extension = input.suffix().toLower();
    if (extension != "txt" && extension != "md")
        throw std::runtime_error("Only TXT and Markdown are supported in this experiment.");
    const auto inlineText = row.value("text");
    if (!inlineText.isUndefined() && !inlineText.isString())
        throw std::runtime_error("Inline document text must be a string.");
    const auto bytes = inlineText.isString() ? inlineText.toString().toUtf8() : readFile(input.absoluteFilePath());
    nookbit::FileFact fact;
    fact.absolutePath = std::filesystem::path(input.absoluteFilePath().toStdWString());
    fact.relativePath = fact.absolutePath.filename();
    fact.extension = extension.toStdString();
    fact.fingerprint.size = static_cast<std::uintmax_t>(bytes.size());
    if (!inlineText.isString()) fact.fingerprint.modifiedUtcMs = input.lastModified().toMSecsSinceEpoch();
    return {input.fileName(), expected.toString(), QString::fromUtf8(bytes).left(4000), std::move(fact)};
}

// CSV의 쉼표, 따옴표와 줄바꿈을 보존하는 함수.
QString csvField(QString value) {
    return '"' + value.replace('"', QStringLiteral("\"\"")) + '"';
}

// 한 분류 방식의 추천과 보류를 채점하고 문서별 결과를 기록하는 함수.
void evaluate(const QString& method, const std::vector<Document>& documents, const Predict& predict,
              QTextStream& csv, QTextStream& summary) {
    int correct = 0, topThree = 0, suggested = 0, wrongOnAbstain = 0;
    qint64 predictionNs = 0;
    for (const auto& document : documents) {
        QElapsedTimer timer;
        timer.start();
        const auto prediction = predict(document);
        const auto elapsed = timer.nsecsElapsed();
        const auto& ranked = prediction.ranked;
        predictionNs += elapsed;
        const auto first = ranked.value(0);
        const bool hit = first == document.expected;
        const bool inTopThree = document.expected.isEmpty() ? ranked.isEmpty() : ranked.contains(document.expected);
        correct += hit;
        topThree += inTopThree;
        suggested += !ranked.isEmpty();
        wrongOnAbstain += document.expected.isEmpty() && !ranked.isEmpty();
        csv << method << ',' << csvField(document.fileName) << ',' << csvField(document.expected);
        for (int i = 0; i < 3; ++i) csv << ',' << csvField(ranked.value(i));
        csv << ',' << (hit ? 1 : 0) << ',' << QString::number(static_cast<double>(elapsed) / 1e6, 'f', 4) << ',';
        if (prediction.bestScore) csv << QString::number(*prediction.bestScore, 'g', 16);
        csv << '\n';
    }
    summary << method << ": top1=" << correct << '/' << documents.size()
            << ", top3=" << topThree << '/' << documents.size()
            << ", suggested=" << suggested << '/' << documents.size()
            << ", wrong_on_abstain=" << wrongOnAbstain
            << ", prediction_ms=" << QString::number(static_cast<double>(predictionNs) / 1e6, 'f', 3) << '\n';
}

} // namespace

// 실험 설정을 읽고 동일한 문서에 규칙과 TF-IDF를 적용하는 진입점.
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addPositionalArgument("manifest", "Experiment JSON manifest.");
    cli.addPositionalArgument("schema", "NookBit v3 rule schema.");
    cli.addPositionalArgument("csv", "A new file for the per-document results.");
    cli.process(app);
    const auto args = cli.positionalArguments();
    if (args.size() != 3) cli.showHelp(2);
    QTextStream summary(stderr);
    try {
        QElapsedTimer timer;
        timer.start();
        const QFileInfo manifest(args[0]);
        QJsonParseError parseError;
        const auto input = QJsonDocument::fromJson(readFile(manifest.absoluteFilePath()), &parseError);
        if (!input.isObject()) throw std::runtime_error("Invalid manifest: " + parseError.errorString().toStdString());
        const auto config = input.object();
        std::vector<Profile> profiles;
        QStringList folders;
        for (const auto& candidate : config.value("candidates").toArray()) {
            const auto row = candidate.toObject();
            const auto folder = row.value("folder").toString();
            const auto description = row.value("description").toString();
            if (folder.isEmpty() || folders.contains(folder))
                throw std::runtime_error("Candidate folders must be nonempty and unique.");
            folders.append(folder);
            profiles.push_back({folder.toStdU16String(), countTextTerms(folder + ' ' + description), {}});
        }
        std::vector<Document> documents;
        for (const auto& row : config.value("documents").toArray()) {
            auto document = readDocument(row.toObject(), manifest);
            if (!document.expected.isEmpty() && !folders.contains(document.expected))
                throw std::runtime_error("Expected folder is missing from the candidate list.");
            documents.push_back(std::move(document));
        }
        const auto ruleFile = config.value("rules").toString();
        if (profiles.empty() || documents.empty() || ruleFile.isEmpty())
            throw std::runtime_error("Manifest requires candidates, documents and a rules file.");
        summary << "input_ms=" << timer.nsecsElapsed() / 1e6 << '\n';
        timer.restart();
        const nookbit::JsonRuleSetRepository repository(QDir(manifest.absolutePath()).absoluteFilePath(ruleFile), args[1]);
        const auto rules = repository.load();
        if (!rules) throw std::runtime_error(rules.error().message);
        summary << "rules_setup_ms=" << timer.nsecsElapsed() / 1e6 << '\n';
        timer.restart();
        const auto idf = prepareProfiles(profiles);
        summary << "tfidf_setup_ms=" << timer.nsecsElapsed() / 1e6 << '\n';
        QFile output(args[2]);
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
            throw std::runtime_error("Cannot create the CSV. Choose a path that does not exist.");
        QTextStream csv(&output);
        csv.setGenerateByteOrderMark(true);
        csv << "method,file,expected,top1,top2,top3,correct,prediction_ms,best_score\n";
        const nookbit::RuleEngine engine;
        const auto now = QDateTime::currentMSecsSinceEpoch();
        evaluate("rules", documents, [&](const Document& document) -> Prediction {
            const auto decision = engine.decide(document.fact, rules.value(), nookbit::MultiMatchPolicy::FirstMatch, now);
            if (!decision) throw std::runtime_error(decision.error().message);
            if (!decision.value() || decision.value()->excluded) return {};
            const auto folder = QString::fromStdWString(decision.value()->destinationRelativePath.generic_wstring());
            return {{folder}, std::nullopt};
        }, csv, summary);
        const auto predictText = [&](const QString& text) {
            const auto prediction = recommend(countTextTerms(text), profiles, idf);
            Prediction result{{}, prediction.bestScore};
            for (const auto& label : prediction.ranked) result.ranked.append(QString::fromStdU16String(label));
            return result;
        };
        evaluate("tfidf_filename", documents, [&](const Document& document) {
            return predictText(document.fileName);
        }, csv, summary);
        evaluate("tfidf_body", documents, [&](const Document& document) {
            return predictText(document.fileName + '\n' + document.body);
        }, csv, summary);
        csv.flush();
        if (csv.status() != QTextStream::Ok || !output.flush()) throw std::runtime_error("CSV write failed.");
        return 0;
    } catch (const std::exception& error) {
        summary << error.what() << '\n';
        return 1;
    }
}

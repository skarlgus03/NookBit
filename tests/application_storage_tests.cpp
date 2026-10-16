#include "app/application_storage.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <iostream>

// 두 검사 실행이 서로와 사용자 저장소에서 격리되고 임시 자료를 정리하는지 검사하는 함수.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setOrganizationName("NookBitProject");
    QCoreApplication::setApplicationName("NookBit");
    int failures = 0;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { ++failures; std::cerr << message << '\n'; }
    };
    QString temporaryRoot;
    {
        nookbit::ApplicationStorage first(true), second(true);
        check(first.valid() && second.valid(), "isolated storage must initialize");
        if (!first.valid() || !second.valid()) return 1;
        check(first.dataDirectory() != second.dataDirectory(), "verification runs must not share data");
        check(first.dataDirectory() != QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
              "verification must not use the user's application directory");
        temporaryRoot = QFileInfo(first.dataDirectory()).absolutePath();
        check(first.settings().format() == QSettings::IniFormat &&
              first.settings().fileName().startsWith(temporaryRoot + '/'), "settings must stay in the temporary root");
        first.settings().setValue("watchSnoozeFolder", "isolated");
        first.settings().sync();
        check(!second.settings().contains("watchSnoozeFolder"), "settings must not leak into another run");
        QFile history(QDir(first.dataDirectory()).filePath("history.sqlite"));
        check(history.open(QIODevice::WriteOnly) && history.write("owned fixture") > 0, "owned database fixture must be writable");
        history.close();
        check(QDir(first.documentsDirectory()).exists() && QDir(first.downloadsDirectory()).exists(),
              "default file locations must also be isolated");
    }
    check(!QFileInfo::exists(temporaryRoot), "verification data must be removed after stores close");
    return failures ? 1 : 0;
}

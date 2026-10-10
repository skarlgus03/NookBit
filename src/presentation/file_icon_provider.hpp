#pragma once
#include <QQuickImageProvider>
#include <QCache>
#include <QMutex>
#include <QMutexLocker>
#include <QFileInfo>
#include <QUrl>
#include <QPainter>
#include <algorithm>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace nookbit {
/// 실제 파일을 읽지 않고 확장자별 시스템 아이콘만 제공하는 표시 전용 창구.
class FileIconProvider final : public QQuickImageProvider {
public:
    /// 아이콘 요청을 화면 스레드 밖에서 처리하고 종류별 캐시를 제한하는 생성자.
    FileIconProvider() : QQuickImageProvider(Image, ForceAsynchronousImageLoading), cache_(128) {}

    /// 파일 이름에서 종류를 구해 캐시 또는 시스템 아이콘을 반환하는 함수.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        const auto suffix = QFileInfo(QUrl::fromPercentEncoding(id.toUtf8())).suffix().toLower().left(64);
        QImage image;
        {
            QMutexLocker lock(&mutex_);
            if (const auto* cached = cache_.object(suffix)) image = *cached;
        }
        if (image.isNull()) {
            image = typeIcon(suffix);
            QMutexLocker lock(&mutex_);
            cache_.insert(suffix, new QImage(image));
        }
        if (size) *size = image.size();
        if (requestedSize.isValid()) {
            const QSize bounded(std::clamp(requestedSize.width(), 1, 256), std::clamp(requestedSize.height(), 1, 256));
            image = image.scaled(bounded, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return image;
    }
private:
    /// 원본 접근 없이 Windows의 파일 형식 아이콘을 얻고 실패하면 기본 그림을 만드는 함수.
    static QImage typeIcon(const QString& suffix) {
#ifdef Q_OS_WIN
        const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto name = suffix.isEmpty() ? QStringLiteral("file") : QStringLiteral("file.") + suffix;
        SHFILEINFOW info{};
        const auto result = SHGetFileInfoW(reinterpret_cast<const wchar_t*>(name.utf16()), FILE_ATTRIBUTE_NORMAL,
            &info, sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
        QImage image;
        if (result && info.hIcon) image = QImage::fromHICON(info.hIcon);
        if (info.hIcon) DestroyIcon(info.hIcon);
        if (SUCCEEDED(initialized)) CoUninitialize();
        if (!image.isNull()) return image;
#endif
        QImage fallback(32, 32, QImage::Format_ARGB32_Premultiplied);
        fallback.fill(Qt::transparent);
        QPainter painter(&fallback);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(QStringLiteral("#7898bb")), 2));
        painter.setBrush(QColor(QStringLiteral("#edf2f8")));
        painter.drawRoundedRect(QRectF(6, 3, 20, 26), 3, 3);
        painter.drawLine(11, 13, 21, 13);
        painter.drawLine(11, 19, 21, 19);
        return fallback;
    }
    QMutex mutex_;
    QCache<QString, QImage> cache_;
};
}

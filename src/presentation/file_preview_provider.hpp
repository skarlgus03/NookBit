#pragma once
#include "presentation/file_icon_provider.hpp"
#include <QImageReader>

namespace nookbit {
/// 작은 이미지 미리보기를 비동기로 만들며 실패한 파일은 종류 아이콘으로 표시한다.
class FilePreviewProvider final : public QQuickImageProvider {
public:
    /// 파일 내용 읽기가 화면 스레드를 막지 않도록 설정하는 생성자.
    FilePreviewProvider() : QQuickImageProvider(Image, ForceAsynchronousImageLoading) {}

    /// 일반 이미지의 첫 프레임을 축소하고 그 밖의 파일은 종류 아이콘을 반환하는 함수.
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        const auto path = QUrl::fromPercentEncoding(id.toUtf8());
        const QFileInfo info(path);
        const auto suffix = info.suffix().toLower();
        const QSize target(requestedSize.width() > 0 ? std::clamp(requestedSize.width(), 1, 256) : 32,
            requestedSize.height() > 0 ? std::clamp(requestedSize.height(), 1, 256) : 32);
        // 과도한 원본 읽기·디코딩을 피하고 일반 사진 형식만 썸네일 대상으로 삼는다.
        const bool imageType = suffix == "png" || suffix == "jpg" || suffix == "jpeg"
            || suffix == "webp" || suffix == "bmp" || suffix == "gif";
        if (imageType && info.isAbsolute() && info.isFile() && info.size() <= 32 * 1024 * 1024) {
            QImageReader reader(path);
            reader.setAutoTransform(true);
            const auto original = reader.size();
            if (original.isValid() && qint64(original.width()) * original.height() <= 16 * 1024 * 1024) {
                reader.setScaledSize(original.scaled(target, Qt::KeepAspectRatio));
                auto preview = reader.read();
                if (!preview.isNull()) {
                    preview = preview.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    if (size) *size = preview.size();
                    return preview;
                }
            }
        }
        return icons_.requestImage(QString::fromUtf8(QUrl::toPercentEncoding(info.fileName())), size, target);
    }
private:
    FileIconProvider icons_;
};
}

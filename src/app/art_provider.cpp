#include "art_provider.hpp"

#include <QDir>
#include <QImage>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>

#include <limits>
#include <memory>

#include "core/library/art.hpp"
#include "core/library/database.hpp"

namespace mp::app {

namespace {

constexpr int kSizes[] = {0, 256, 600};

bool validHash(const QString& hash) {
    if (hash.size() != 16) {
        return false;  // also keeps "../" and other path tricks out of the file names below
    }
    for (const QChar c : hash) {
        if (!((c >= QLatin1Char('0') && c <= QLatin1Char('9')) || (c >= QLatin1Char('a') && c <= QLatin1Char('f')))) {
            return false;
        }
    }
    return true;
}

// One read-only-in-practice connection per pool thread, opened on first use.
core::library::Database& threadDatabase(const std::filesystem::path& file) {
    thread_local std::unique_ptr<core::library::Database> db;
    if (!db) {
        db = std::make_unique<core::library::Database>(file);
    }
    return *db;
}

class ArtResponse : public QQuickImageResponse, public QRunnable {
public:
    ArtResponse(QString hash, int size, std::filesystem::path databaseFile, QString cacheFolder)
        : hash_(std::move(hash)),
          size_(size),
          databaseFile_(std::move(databaseFile)),
          cacheFolder_(std::move(cacheFolder)) {
        setAutoDelete(false);  // owned by the QML engine, which deletes it after reading the result
    }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return error_; }
    void cancel() override { cancelled_ = true; }

    void run() override {
        if (!cancelled_) {
            load();
        }
        emit finished();
    }

private:
    QString thumbnailPath() const { return QDir(cacheFolder_).filePath(QStringLiteral("%1_%2.jpg").arg(hash_).arg(size_)); }

    void load() {
        if (size_ > 0 && image_.load(thumbnailPath())) {
            return;
        }
        try {
            std::string source;
            {
                auto s = threadDatabase(databaseFile_).prepare(
                    "SELECT art_source FROM albums WHERE art_hash = ?1 AND art_source IS NOT NULL LIMIT 1");
                s.bind(1, hash_.toStdString());
                if (s.step()) {
                    source = s.getText(0);
                }
            }
            if (source.empty()) {
                error_ = QStringLiteral("no cover with hash %1").arg(hash_);
                return;
            }
            const core::library::Bytes bytes = core::library::loadArtSource(source);
            QImage original = QImage::fromData(bytes.data(), static_cast<qsizetype>(bytes.size()));
            if (original.isNull()) {
                error_ = QStringLiteral("cover could not be decoded: %1").arg(QString::fromStdString(source));
                return;
            }
            if (size_ == 0) {
                image_ = original;
                return;
            }
            // Never scale up; JPEG has no alpha, so flatten first.
            QImage scaled = original.width() > size_ || original.height() > size_
                                ? original.scaled(size_, size_, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                                : original;
            image_ = scaled.convertToFormat(QImage::Format_RGB32);
            QDir().mkpath(cacheFolder_);
            QSaveFile out(thumbnailPath());  // written to a temp name and renamed: a reader never sees half a file
            if (out.open(QIODevice::WriteOnly)) {
                image_.save(&out, "JPEG", 90);
                out.commit();
            }
        } catch (const std::exception& e) {
            error_ = QString::fromUtf8(e.what());
        }
    }

    QString hash_;
    int size_;
    std::filesystem::path databaseFile_;
    QString cacheFolder_;
    QImage image_;
    QString error_;
    std::atomic<bool> cancelled_{false};
};

}  // namespace

ArtProvider::ArtProvider(const std::filesystem::path& databaseFile, const QString& cacheFolder)
    : databaseFile_(databaseFile), cacheFolder_(cacheFolder) {
    pool_.setMaxThreadCount(4);
}

ArtProvider::~ArtProvider() { pool_.waitForDone(); }

QString ArtProvider::defaultCacheFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("art"));
}

QQuickImageResponse* ArtProvider::requestImageResponse(const QString& id, const QSize& /*requestedSize*/) {
    // id = "<hash>/<size>"
    const QStringList parts = id.split(QLatin1Char('/'));
    int size = -1;
    if (parts.size() == 2) {
        bool ok = false;
        const int value = parts.at(1).toInt(&ok);
        for (const int allowed : kSizes) {
            if (ok && value == allowed) {
                size = value;
            }
        }
    }
    auto* response = new ArtResponse(parts.value(0), size < 0 ? 256 : size, databaseFile_, cacheFolder_);
    if (size < 0 || !validHash(parts.value(0))) {
        // Bad id: finish at once with an empty image (QML shows the Image as errored).
        QMetaObject::invokeMethod(response, [response] { emit response->finished(); }, Qt::QueuedConnection);
        return response;
    }
    // Newest first: when the grid scrolls fast, the covers now on screen jump the queue; older requests whose
    // cards were destroyed meanwhile are cancelled and skip their work.
    // (Wraps after ~2 billion requests; the order is briefly wrong once, nothing else.)
    pool_.start(response, nextPriority_.fetch_add(1, std::memory_order_relaxed) & std::numeric_limits<int>::max());
    return response;
}

}  // namespace mp::app

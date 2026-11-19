#include "art_provider.hpp"

#include <QBuffer>
#include <QCache>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QQuickImageResponse>
#include <QQuickTextureFactory>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>

#include <limits>
#include <memory>

#include "core/library/art.hpp"
#include "core/library/database.hpp"

namespace mp::app {

struct ArtProvider::Shared {
    std::filesystem::path databaseFile;
    QString cacheFolder;
    std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> prewarmGeneration{0};  // bumped by prewarm(): older queued jobs skip

    // Decoded thumbnails by "<hash>_<size>". Cost in KB; 200 MB ≈ 780 covers at 256 px.
    QMutex mutex;
    QCache<QString, QImage> memory{200 * 1024};

    bool cached(const QString& key, QImage& out) {
        QMutexLocker lock(&mutex);
        if (const QImage* image = memory.object(key)) {
            out = *image;  // implicitly shared: no pixel copy
            return true;
        }
        return false;
    }
    void remember(const QString& key, const QImage& image) {
        QMutexLocker lock(&mutex);
        memory.insert(key, new QImage(image), std::max<qsizetype>(1, image.sizeInBytes() / 1024));
    }
    QString thumbnailPath(const QString& hash, int size) const {
        return QDir(cacheFolder).filePath(QStringLiteral("%1_%2.jpg").arg(hash).arg(size));
    }
};

namespace {

constexpr int kSizes[] = {0, 256, 600, 1200};  // 1200: Now Playing (D46)

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

QString memoryKey(const QString& hash, int size) { return QStringLiteral("%1_%2").arg(hash).arg(size); }

// One read-only-in-practice connection per pool thread, opened on first use.
core::library::Database& threadDatabase(const std::filesystem::path& file) {
    thread_local std::unique_ptr<core::library::Database> db;
    if (!db) {
        db = std::make_unique<core::library::Database>(file);
    }
    return *db;
}

// The cover's source bytes (embedded picture or cover file), or empty with `error` set.
core::library::Bytes sourceBytes(const ArtProvider::Shared& shared, const QString& hash, QString& error) {
    std::string source;
    auto s = threadDatabase(shared.databaseFile)
                 .prepare("SELECT art_source FROM albums WHERE art_hash = ?1 AND art_source IS NOT NULL LIMIT 1");
    s.bind(1, hash.toStdString());
    if (s.step()) {
        source = s.getText(0);
    }
    if (source.empty()) {
        error = QStringLiteral("no cover with hash %1").arg(hash);
        return {};
    }
    core::library::Bytes bytes = core::library::loadArtSource(source);
    if (bytes.empty()) {
        error = QStringLiteral("cover source is gone: %1").arg(QString::fromStdString(source));
    }
    return bytes;
}

// Decodes `bytes`, at most `maxSide` px on the longer side (0 = full size). JPEGs are decoded straight at reduced
// size (1/2, 1/4, 1/8 in the decoder), several times faster than a full decode and a scale.
QImage decode(const core::library::Bytes& bytes, int maxSide) {
    QByteArray data = QByteArray::fromRawData(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    QBuffer buffer(&data);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    if (maxSide > 0) {
        const QSize full = reader.size();
        if (full.isValid() && (full.width() > maxSide || full.height() > maxSide)) {
            reader.setScaledSize(full.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
            reader.setQuality(100);  // smooth final scaling step
        }
    }
    QImage image = reader.read();  // decoded into its own buffer, independent of `bytes`
    if (!image.isNull() && maxSide > 0 && (image.width() > maxSide || image.height() > maxSide)) {
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);  // reader couldn't
    }
    return image;
}

// Writes a thumbnail file (JPEG, no alpha), through a temp name + rename so a reader never sees half a file.
QImage writeThumbnail(const ArtProvider::Shared& shared, const QString& hash, int size, const QImage& image) {
    QImage flat = image.convertToFormat(QImage::Format_RGB32);
    QDir().mkpath(shared.cacheFolder);
    QSaveFile out(shared.thumbnailPath(hash, size));
    if (out.open(QIODevice::WriteOnly)) {
        flat.save(&out, "JPEG", 90);
        out.commit();
    }
    return flat;
}

class ArtResponse : public QQuickImageResponse, public QRunnable {
public:
    ArtResponse(QString hash, int size, std::shared_ptr<ArtProvider::Shared> shared)
        : hash_(std::move(hash)), size_(size), shared_(std::move(shared)) {
        setAutoDelete(false);  // owned by the QML engine, which deletes it after reading the result
    }

    // Answers at once with an image that is already in memory (still asynchronously, as QML expects).
    void finishWith(const QImage& image) {
        image_ = image;
        QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
    }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(image_); }
    QString errorString() const override { return error_; }
    void cancel() override { cancelled_ = true; }

    void run() override {
        if (!cancelled_ && !shared_->stopping) {
            load();
        }
        emit finished();
    }

private:
    void load() {
        const QString key = memoryKey(hash_, size_);
        if (size_ > 0 && shared_->cached(key, image_)) {
            return;  // another request (or the same cover at another place) decoded it meanwhile
        }
        if (size_ > 0 && image_.load(shared_->thumbnailPath(hash_, size_))) {
            shared_->remember(key, image_);
            return;
        }
        try {
            const core::library::Bytes bytes = sourceBytes(*shared_, hash_, error_);
            if (bytes.empty()) {
                return;
            }
            const QImage image = decode(bytes, size_);
            if (image.isNull()) {
                error_ = QStringLiteral("cover %1 could not be decoded").arg(hash_);
                return;
            }
            if (size_ == 0) {
                image_ = image;
                return;
            }
            image_ = writeThumbnail(*shared_, hash_, size_, image);
            shared_->remember(key, image_);
        } catch (const std::exception& e) {
            error_ = QString::fromUtf8(e.what());
        }
    }

    QString hash_;
    int size_;
    std::shared_ptr<ArtProvider::Shared> shared_;
    QImage image_;
    QString error_;
    std::atomic<bool> cancelled_{false};
};

// Background job: the missing 600 and 256 px thumbnail files of one cover, from one source decode.
class PrewarmJob : public QRunnable {
public:
    PrewarmJob(QString hash, std::uint64_t generation, std::shared_ptr<ArtProvider::Shared> shared)
        : hash_(std::move(hash)), generation_(generation), shared_(std::move(shared)) {
        setAutoDelete(true);
    }

    void run() override {
        if (shared_->stopping || shared_->prewarmGeneration != generation_) {
            return;  // app closing, or a newer prewarm() replaced this queue
        }
        QThread::currentThread()->setPriority(QThread::LowPriority);
        const bool need600 = !QFileInfo::exists(shared_->thumbnailPath(hash_, 600));
        const bool need256 = !QFileInfo::exists(shared_->thumbnailPath(hash_, 256));
        if (!need600 && !need256) {
            return;
        }
        try {
            QString error;
            const core::library::Bytes bytes = sourceBytes(*shared_, hash_, error);
            if (bytes.empty()) {
                return;
            }
            const QImage big = decode(bytes, 600);
            if (big.isNull()) {
                return;
            }
            if (need600) {
                writeThumbnail(*shared_, hash_, 600, big);
            }
            if (need256) {
                writeThumbnail(*shared_, hash_, 256, big.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
        } catch (...) {
            // a cover that can't be read now is simply made on demand later
        }
    }

private:
    QString hash_;
    std::uint64_t generation_;
    std::shared_ptr<ArtProvider::Shared> shared_;
};

}  // namespace

ArtProvider::ArtProvider(const std::filesystem::path& databaseFile, const QString& cacheFolder)
    : shared_(std::make_shared<Shared>()) {
    shared_->databaseFile = databaseFile;
    shared_->cacheFolder = cacheFolder;
    pool_.setMaxThreadCount(4);
    prewarmPool_.setMaxThreadCount(2);  // leaves the interactive pool and the disk mostly to what's on screen
}

ArtProvider::~ArtProvider() {
    shared_->stopping = true;
    prewarmPool_.clear();
    prewarmPool_.waitForDone();
    pool_.waitForDone();
}

QString ArtProvider::defaultCacheFolder() {
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("art"));
}

void ArtProvider::prewarm(const QStringList& hashes) {
    const std::uint64_t generation = ++shared_->prewarmGeneration;
    prewarmPool_.clear();  // drop jobs not started yet; running ones finish their cover
    for (const QString& hash : hashes) {
        if (validHash(hash)) {
            prewarmPool_.start(new PrewarmJob(hash, generation, shared_));
        }
    }
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
    const QString hash = parts.value(0);
    auto* response = new ArtResponse(hash, size < 0 ? 256 : size, shared_);
    if (size < 0 || !validHash(hash)) {
        // Bad id: finish at once with an empty image (QML shows the Image as errored).
        QMetaObject::invokeMethod(response, [response] { emit response->finished(); }, Qt::QueuedConnection);
        return response;
    }
    // Already decoded: no thread, no disk.
    QImage image;
    if (size > 0 && shared_->cached(memoryKey(hash, size), image)) {
        response->finishWith(image);
        return response;
    }
    // Newest first: when the grid scrolls fast, the covers now on screen jump the queue; older requests whose
    // cards were destroyed meanwhile are cancelled and skip their work.
    // (Wraps after ~2 billion requests; the order is briefly wrong once, nothing else.)
    pool_.start(response, nextPriority_.fetch_add(1, std::memory_order_relaxed) & std::numeric_limits<int>::max());
    return response;
}

}  // namespace mp::app

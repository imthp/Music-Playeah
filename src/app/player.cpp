#include "player.hpp"

#include <QCollator>
#include <QDir>
#include <QDirListing>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>

#include "core/format.hpp"

namespace mp::app {

using core::audio::PlaybackState;
using core::audio::TrackRequest;

namespace {

// Now Playing's signal path card (v1: WASAPI shared, float32, resampled to the device rate; SPEC §5).
QVariantList describeSignalPath(const core::audio::TrackInfo& info, int outputRate, const std::string& device) {
    auto line = [](const char* label, const QString& value) {
        return QVariantMap{{QStringLiteral("label"), QString::fromLatin1(label)}, {QStringLiteral("value"), value}};
    };
    const QString khz = QString::fromStdString(core::formatKilohertz(info.sampleRate));
    const QString outKhz = QString::fromStdString(core::formatKilohertz(outputRate));
    QStringList source{QString::fromStdString(info.codec)};
    if (info.dsd) {
        source << QStringLiteral("DSD%1").arg(info.sampleRate / 44'100);
    } else {
        if (info.bitDepth > 0) {
            source << QStringLiteral("%1-bit").arg(info.bitDepth);
        } else if (info.bitrateKbps > 0) {
            source << QStringLiteral("%1 kbps").arg(info.bitrateKbps);
        }
        source << khz + QStringLiteral(" kHz");
    }
    source << (info.channels == 1 ? QStringLiteral("mono") : info.channels == 2 ? QStringLiteral("stereo")
                                                                                 : QStringLiteral("%1 channels").arg(info.channels));
    QString resample;
    if (info.dsd) {
        resample = QStringLiteral("DSD → PCM %1 kHz").arg(outKhz);
    } else if (outputRate > 0 && info.sampleRate != outputRate) {
        resample = QStringLiteral("%1 → %2 kHz").arg(khz, outKhz);
    } else {
        resample = QStringLiteral("None");
    }
    return {line("Source", source.join(QStringLiteral(" · "))),
            line("Resample", resample),
#ifdef _WIN32
            line("Output", QString::fromStdString(device) + QStringLiteral(" · WASAPI shared · 32-bit float"))};
#else
            line("Output", QString::fromStdString(device) + QStringLiteral(" · shared · 32-bit float"))};
#endif
}

const QStringList kAudioPatterns = {"*.flac", "*.mp3", "*.m4a", "*.aac", "*.alac", "*.dsf", "*.dff",
                                    "*.wav",  "*.aif", "*.aiff", "*.ogg", "*.oga", "*.opus", "*.wv", "*.ape"};

// Audio files in `folder` and its subfolders (CD1/CD2...), in natural order ("2" before "10").
QStringList audioFilesIn(const QString& folder) {
    // QDirListing (Qt 6.8+) replaces the deprecated QDirIterator. Recursive goes into every
    // subfolder; the name filters only pick which files are returned.
    QStringList files;
    using F = QDirListing::IteratorFlag;
    for (const auto& entry : QDirListing(folder, kAudioPatterns, F::Recursive | F::FilesOnly)) {
        files << entry.filePath();
    }
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(),
              [&](const QString& a, const QString& b) { return collator.compare(a, b) < 0; });
    return files;
}

}  // namespace

// Receives engine events on the engine thread and re-posts them to the Player's (UI) thread.
// Qt drops a queued call whose context object was deleted, so nothing runs after ~Player.
class Player::Listener : public core::audio::EngineListener {
public:
    explicit Listener(Player* player) : player_(player) {}

    void onTrackStarted(std::uint64_t id) override {
        QMetaObject::invokeMethod(player_, [p = player_, id] { p->handleTrackStarted(id); }, Qt::QueuedConnection);
    }
    void onStateChanged(PlaybackState state) override {
        QMetaObject::invokeMethod(player_, [p = player_, state] { p->handleStateChanged(state); }, Qt::QueuedConnection);
    }
    void onEndOfQueue() override {
        QMetaObject::invokeMethod(player_, [p = player_] { p->handleEndOfQueue(); }, Qt::QueuedConnection);
    }
    void onError(std::uint64_t id, const std::string& message) override {
        const QString text = QString::fromStdString(message);
        QMetaObject::invokeMethod(player_, [p = player_, id, text] { p->handleError(id, text); }, Qt::QueuedConnection);
    }

private:
    Player* player_;
};

Player::Player(QObject* parent)
    : QObject(parent),
      listener_(std::make_unique<Listener>(this)),
      engine_(std::make_unique<core::audio::Engine>(listener_.get())) {
    engine_->setVolume(static_cast<float>(volume_));
    std::string error;
    if (engine_->start(error)) {
        deviceText_ = QStringLiteral("%1 · %2 kHz shared")
                          .arg(QString::fromStdString(engine_->deviceName()),
                               QString::fromStdString(core::formatKilohertz(engine_->outputRate())));
    } else {
        deviceText_ = QStringLiteral("No audio device");
        errorText_ = QString::fromStdString(error);
    }

    pollTimer_.setInterval(100);  // SPEC §4: position updates ~10 per second
    connect(&pollTimer_, &QTimer::timeout, this, &Player::poll);
    pollTimer_.start();
}

Player::~Player() = default;

// ------------------------------------------------------------------ properties

QString Player::title() const {
    if (currentIndex_ < 0 || currentIndex_ >= names_.size()) {
        return {};
    }
    return names_.at(currentIndex_);
}

QVariantMap Player::current() const {
    if (currentIndex_ < 0 || currentIndex_ >= items_.size()) {
        return {};
    }
    return items_.at(currentIndex_).toMap();
}

QString Player::positionText() const { return QString::fromStdString(core::formatDuration(positionMs_)); }
QString Player::durationText() const { return QString::fromStdString(core::formatDuration(durationMs_)); }

void Player::setVolume(qreal volume) {
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume_ + 1.0, volume + 1.0)) {
        return;
    }
    volume_ = volume;
    engine_->setVolume(static_cast<float>(volume_));
    emit volumeChanged();
}

// ------------------------------------------------------------------ commands

void Player::openUrl(const QUrl& url) { openPath(url.toLocalFile()); }

void Player::openPath(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        setError(QStringLiteral("Not found: %1").arg(path));
        return;
    }
    const QString folder = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    paths_ = audioFilesIn(folder);
    names_.clear();
    items_.clear();
    const QDir base(folder);
    for (const QString& p : paths_) {
        names_ << QDir::toNativeSeparators(base.relativeFilePath(p));
    }
    folder_ = QDir::toNativeSeparators(folder);
    currentIndex_ = -1;
    emit trackListChanged();
    emit currentChanged();

    if (paths_.isEmpty()) {
        setError(QStringLiteral("No audio files in %1").arg(folder_));
        return;
    }
    setError({});
    const int start = info.isDir() ? 0 : std::max(0, static_cast<int>(paths_.indexOf(info.absoluteFilePath())));
    playIndex(start);
}

void Player::playTracks(const QVariantList& items, int start, const QString& label) {
    paths_.clear();
    names_.clear();
    items_ = items;
    for (const QVariant& item : items) {
        const QVariantMap map = item.toMap();
        paths_ << map.value(QStringLiteral("path")).toString();
        names_ << map.value(QStringLiteral("title")).toString();
    }
    folder_ = label;
    currentIndex_ = -1;
    emit trackListChanged();
    emit currentChanged();
    if (paths_.isEmpty()) {
        setError(QStringLiteral("Nothing to play"));
        return;
    }
    setError({});
    playIndex(std::clamp(start, 0, static_cast<int>(paths_.size()) - 1));
}

void Player::playIndex(int index) {
    if (index < 0 || index >= paths_.size()) {
        return;
    }
    pendingPlay_ = index;
    engine_->play(request(index));
    queueNextAfter(index);
}

void Player::playPause() {
    switch (state_) {
        case PlaybackState::Playing: engine_->pause(); break;
        case PlaybackState::Paused: engine_->resume(); break;
        case PlaybackState::Stopped: playIndex(std::max(0, currentIndex_)); break;
    }
}

void Player::next() {
    if (currentIndex_ + 1 < paths_.size()) {
        playIndex(currentIndex_ + 1);
    }
}

void Player::previous() {
    // Like most players: the first press restarts the track, a quick second press goes back.
    if (positionMs_ > 3000 || currentIndex_ <= 0) {
        seek(0);
    } else {
        playIndex(currentIndex_ - 1);
    }
}

void Player::stop() { engine_->stop(); }

void Player::seek(qint64 ms) {
    engine_->seek(ms);
    positionMs_ = std::clamp<qint64>(ms, 0, durationMs_ > 0 ? durationMs_ : ms);  // instant feedback
    emit positionChanged();
}

void Player::seekBy(qint64 deltaMs) { seek(positionMs_ + deltaMs); }

// ------------------------------------------------------------------ engine events

void Player::handleTrackStarted(std::uint64_t id) {
    currentIndex_ = static_cast<int>(id);
    pendingPlay_ = -1;
    const auto status = engine_->status();
    const bool known = status.hasTrack && status.trackId == id;
    badge_ = known ? QString::fromStdString(core::formatBadge(status.info)) : QString();
    signalPath_ = known ? describeSignalPath(status.info, engine_->outputRate(), engine_->deviceName()) : QVariantList{};
    emit currentChanged();
    queueNextAfter(currentIndex_);  // keeps one track of look-ahead for gapless playback
}

void Player::handleStateChanged(PlaybackState state) {
    state_ = state;
    emit stateChanged();
}

void Player::handleEndOfQueue() {
    poll();
}

void Player::handleError(std::uint64_t id, const QString& message) {
    const int index = static_cast<int>(id);
    setError(QStringLiteral("%1: %2").arg(index >= 0 && index < names_.size() ? names_.at(index) : QString(), message));
    // Skip files that can't be played.
    if (index == pendingPlay_) {
        pendingPlay_ = -1;
        playIndex(index + 1);  // the track we asked to play now: try the one after it
    } else {
        queueNextAfter(index);  // the pre-opened next track: queue the one after it instead
    }
}

// ------------------------------------------------------------------ helpers

void Player::poll() {
    const auto s = engine_->status();
    if (s.state != state_) {
        state_ = s.state;
        emit stateChanged();
    }
    if (s.positionMs != positionMs_ || s.durationMs != durationMs_ || s.underruns != underruns_) {
        positionMs_ = s.positionMs;
        durationMs_ = s.durationMs;
        underruns_ = s.underruns;
        emit positionChanged();
    }
}

void Player::queueNextAfter(int index) {
    if (index + 1 < paths_.size()) {
        engine_->setNext(request(index + 1));
    } else {
        engine_->setNext(std::nullopt);
    }
}

TrackRequest Player::request(int index) const {
    return TrackRequest{static_cast<std::uint64_t>(index), paths_.at(index).toUtf8().toStdString()};
}

void Player::setError(const QString& text) {
    if (text != errorText_) {
        errorText_ = text;
        emit errorTextChanged();
    }
}

}  // namespace mp::app

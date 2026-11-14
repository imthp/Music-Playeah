#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "core/audio/engine.hpp"

namespace mp::app {

// Milestone 2 test controller, exposed to QML as the singleton `Player`.
// Bridges the Qt-free engine to QML: commands go straight to the engine (they never block),
// engine events are re-posted to the UI thread, and the position is polled 10 times a second.
// The "playlist" is just the audio files of one folder; the real queue comes in milestone 5.
class Player : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY stateChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY trackListChanged)
    Q_PROPERTY(QStringList trackNames READ trackNames NOTIFY trackListChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(QString title READ title NOTIFY currentChanged)
    Q_PROPERTY(QString badge READ badge NOTIFY currentChanged)
    Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY positionChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY positionChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY positionChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(int underruns READ underruns NOTIFY positionChanged)
    Q_PROPERTY(QString deviceText READ deviceText CONSTANT)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit Player(QObject* parent = nullptr);
    ~Player() override;

    bool playing() const { return state_ == core::audio::PlaybackState::Playing; }
    bool paused() const { return state_ == core::audio::PlaybackState::Paused; }
    QString folder() const { return folder_; }
    QStringList trackNames() const { return names_; }
    int currentIndex() const { return currentIndex_; }
    QString title() const;
    QString badge() const { return badge_; }
    qint64 positionMs() const { return positionMs_; }
    qint64 durationMs() const { return durationMs_; }
    QString positionText() const;
    QString durationText() const;
    qreal volume() const { return volume_; }
    void setVolume(qreal volume);
    int underruns() const { return static_cast<int>(underruns_); }
    QString deviceText() const { return deviceText_; }
    QString errorText() const { return errorText_; }

    // A folder (plays from the first file) or a single file (plays its folder from that file).
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    Q_INVOKABLE void playIndex(int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void seekBy(qint64 deltaMs);

signals:
    void stateChanged();
    void trackListChanged();
    void currentChanged();
    void positionChanged();
    void volumeChanged();
    void errorTextChanged();

private:
    class Listener;

    // Engine events, already moved to the UI thread.
    void handleTrackStarted(std::uint64_t id);
    void handleStateChanged(core::audio::PlaybackState state);
    void handleEndOfQueue();
    void handleError(std::uint64_t id, const QString& message);

    void poll();
    void queueNextAfter(int index);
    core::audio::TrackRequest request(int index) const;
    void setError(const QString& text);

    QString folder_;
    QStringList paths_;
    QStringList names_;
    int currentIndex_ = -1;
    int pendingPlay_ = -1;  // asked to play, not audible yet
    QString badge_;
    qint64 positionMs_ = 0;
    qint64 durationMs_ = 0;
    qreal volume_ = 0.8;
    std::uint64_t underruns_ = 0;
    QString deviceText_;
    QString errorText_;
    core::audio::PlaybackState state_ = core::audio::PlaybackState::Stopped;
    QTimer pollTimer_;

    // Order matters: the engine is destroyed first (its thread stops), then the listener.
    std::unique_ptr<Listener> listener_;
    std::unique_ptr<core::audio::Engine> engine_;
};

}  // namespace mp::app

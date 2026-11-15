#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

#include "core/library/library.hpp"
#include "core/library/scanner.hpp"

namespace mp::app {

// Milestone 3a test controller, exposed to QML as the singleton `Library`.
// Bridges the Qt-free core::library::Library (database, watched folders, counts) and Scanner
// (own threads) to QML. Scanner callbacks arrive on scanner threads and are re-posted to the UI
// thread; all database access from here happens on the UI thread (the scanner has its own connections).
// The real library models (albums, artists, songs) come in milestone 4.
class LibraryController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Library)
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(QString databasePath READ databasePath CONSTANT)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(bool indeterminate READ indeterminate NOTIFY progressChanged)
    Q_PROPERTY(qreal progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressText READ progressText NOTIFY progressChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY countsChanged)
    Q_PROPERTY(int albumCount READ albumCount NOTIFY countsChanged)
    Q_PROPERTY(int artistCount READ artistCount NOTIFY countsChanged)
    Q_PROPERTY(int missingCount READ missingCount NOTIFY countsChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY summaryChanged)
    Q_PROPERTY(QStringList problems READ problems NOTIFY summaryChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    explicit LibraryController(QObject* parent = nullptr);
    ~LibraryController() override;

    bool available() const { return library_ != nullptr; }
    QString databasePath() const { return databasePath_; }
    QStringList folders() const { return folders_; }
    bool scanning() const { return scanning_; }
    bool indeterminate() const { return indeterminate_; }
    qreal progress() const { return progress_; }
    QString progressText() const { return progressText_; }
    int trackCount() const { return trackCount_; }
    int albumCount() const { return albumCount_; }
    int artistCount() const { return artistCount_; }
    int missingCount() const { return missingCount_; }
    QString summaryText() const { return summaryText_; }
    QStringList problems() const { return problems_; }
    QString errorText() const { return errorText_; }

    // Adds a music folder (from a FolderDialog) and scans it.
    Q_INVOKABLE void addFolder(const QUrl& folder);
    // Stops watching a folder (path as listed in `folders`). Its tracks are kept, marked missing.
    Q_INVOKABLE void removeFolder(const QString& path);
    // Scans all watched folders (incremental).
    Q_INVOKABLE void scan();
    Q_INVOKABLE void cancelScan();

signals:
    void foldersChanged();
    void scanningChanged();
    void progressChanged();
    void countsChanged();
    void summaryChanged();
    void errorTextChanged();

private:
    void handleProgress(const core::library::ScanProgress& progress);
    void handleDone(const core::library::ScanSummary& summary);
    void refreshFolders();
    void refreshCounts();
    void setError(const QString& text);

    QString databasePath_;
    QStringList folders_;
    bool scanning_ = false;
    bool indeterminate_ = false;
    qreal progress_ = 0;
    QString progressText_;
    int trackCount_ = 0;
    int albumCount_ = 0;
    int artistCount_ = 0;
    int missingCount_ = 0;
    QString summaryText_;
    QStringList problems_;
    QString errorText_;

    // Order matters: the scanner (its threads) is destroyed before the library it reads from.
    std::unique_ptr<core::library::Library> library_;
    std::unique_ptr<core::library::Scanner> scanner_;
};

}  // namespace mp::app

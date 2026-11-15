#include "library_controller.hpp"

#include <QDir>
#include <QLocale>
#include <QMetaObject>
#include <QStandardPaths>

#include <exception>

#include "core/text.hpp"

namespace mp::app {

namespace {

std::filesystem::path toPath(const QString& text) { return core::pathFromUtf8(text.toUtf8().toStdString()); }

QString fromUtf8(const std::string& text) { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); }

QString number(std::uint64_t n) { return QLocale().toString(static_cast<qulonglong>(n)); }

QString seconds(std::int64_t ms) { return QLocale().toString(static_cast<double>(ms) / 1000.0, 'f', 1) + QStringLiteral(" s"); }

}  // namespace

LibraryController::LibraryController(QObject* parent) : QObject(parent) {
    // %LOCALAPPDATA% on Windows (SPEC §6); never a hard-coded path (D22).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    const QString file = QDir(dir).filePath(QStringLiteral("library.db"));
    databasePath_ = QDir::toNativeSeparators(file);
    try {
        library_ = std::make_unique<core::library::Library>(toPath(file));
        scanner_ = std::make_unique<core::library::Scanner>(library_->dbFile());
        refreshFolders();
        refreshCounts();
    } catch (const std::exception& e) {
        library_.reset();
        errorText_ = tr("Could not open the library database: %1").arg(QString::fromUtf8(e.what()));
    }
}

LibraryController::~LibraryController() = default;

void LibraryController::setError(const QString& text) {
    if (errorText_ != text) {
        errorText_ = text;
        emit errorTextChanged();
    }
}

void LibraryController::refreshFolders() {
    folders_.clear();
    for (const auto& folder : library_->folders()) {
        folders_ << QDir::toNativeSeparators(fromUtf8(folder.path));
    }
    emit foldersChanged();
}

void LibraryController::refreshCounts() {
    const auto counts = library_->counts();
    trackCount_ = static_cast<int>(counts.tracks);
    albumCount_ = static_cast<int>(counts.albums);
    artistCount_ = static_cast<int>(counts.artists);
    missingCount_ = static_cast<int>(counts.missing);
    emit countsChanged();
}

void LibraryController::addFolder(const QUrl& folder) {
    if (!library_) {
        return;
    }
    if (scanning_) {
        setError(tr("Wait for the running scan to finish first."));
        return;
    }
    try {
        const auto result = library_->addFolder(toPath(folder.toLocalFile()));
        if (!result.ok) {
            setError(fromUtf8(result.error));
            return;
        }
        setError({});
        refreshFolders();
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
        return;
    }
    scan();
}

void LibraryController::removeFolder(const QString& path) {
    if (!library_) {
        return;
    }
    if (scanning_) {
        setError(tr("Wait for the running scan to finish first."));
        return;
    }
    try {
        library_->removeFolder(QDir::fromNativeSeparators(path).toUtf8().toStdString());  // stored with '/' separators
        setError({});
        refreshFolders();
        refreshCounts();
    } catch (const std::exception& e) {
        setError(QString::fromUtf8(e.what()));
    }
}

void LibraryController::scan() {
    if (!library_ || scanning_) {
        return;
    }
    if (folders_.isEmpty()) {
        setError(tr("Add a music folder first."));
        return;
    }
    setError({});
    scanning_ = true;
    indeterminate_ = true;
    progress_ = 0;
    progressText_ = tr("Starting…");
    summaryText_.clear();
    problems_.clear();
    emit scanningChanged();
    emit progressChanged();
    emit summaryChanged();

    // Both callbacks run on scanner threads. The queued call is dropped if `this` is gone.
    const bool started = scanner_->start(
        [this](const core::library::ScanProgress& p) {
            QMetaObject::invokeMethod(this, [this, p] { handleProgress(p); }, Qt::QueuedConnection);
        },
        [this](const core::library::ScanSummary& s) {
            QMetaObject::invokeMethod(this, [this, s] { handleDone(s); }, Qt::QueuedConnection);
        });
    if (!started) {
        scanning_ = false;
        emit scanningChanged();
    }
}

void LibraryController::cancelScan() {
    if (scanner_ && scanning_) {
        scanner_->cancel();
        progressText_ = tr("Cancelling…");
        emit progressChanged();
    }
}

void LibraryController::handleProgress(const core::library::ScanProgress& p) {
    if (!scanning_) {
        return;  // a late update after the scan finished
    }
    using Phase = core::library::ScanProgress::Phase;
    if (p.phase == Phase::Finishing) {
        indeterminate_ = true;
        progressText_ = tr("Finishing: albums and artists…");
    } else if (!p.walkFinished) {
        // Still walking the folders: the total isn't known yet.
        indeterminate_ = true;
        progressText_ = tr("%1: %2 files found, %3 handled")
                            .arg(QDir::toNativeSeparators(fromUtf8(p.folder)), number(p.filesFound), number(p.filesDone));
    } else {
        indeterminate_ = false;
        progress_ = p.filesFound > 0 ? static_cast<qreal>(p.filesDone) / static_cast<qreal>(p.filesFound) : 1.0;
        progressText_ = tr("%1 of %2 files").arg(number(p.filesDone), number(p.filesFound));
    }
    emit progressChanged();
}

void LibraryController::handleDone(const core::library::ScanSummary& s) {
    scanning_ = false;
    indeterminate_ = false;
    progress_ = s.cancelled ? progress_ : 1.0;
    progressText_.clear();
    refreshCounts();

    const QString headline = s.cancelled ? tr("Scan cancelled after %1.").arg(seconds(s.elapsedMs))
                                         : tr("Scan finished in %1.").arg(seconds(s.elapsedMs));
    summaryText_ = tr("%1 %2 files: %3 new, %4 updated, %5 unchanged, %6 restored, %7 moved, %8 missing, %9 failed.")
                       .arg(headline, number(s.filesSeen), number(s.added), number(s.updated), number(s.unchanged),
                            number(s.restored), number(s.moved), number(s.markedMissing), number(s.failed));

    problems_.clear();
    if (!s.fatalError.empty()) {
        problems_ << tr("Scan stopped: %1").arg(fromUtf8(s.fatalError));
    }
    for (const std::string& folder : s.unavailableFolders) {
        problems_ << tr("Folder not available (unplugged or renamed?): %1")
                         .arg(QDir::toNativeSeparators(fromUtf8(folder)));
    }
    for (const std::string& error : s.errors) {
        problems_ << fromUtf8(error);
    }
    emit scanningChanged();
    emit progressChanged();
    emit summaryChanged();
}

}  // namespace mp::app

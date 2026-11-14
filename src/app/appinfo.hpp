#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace mp::app {

// Read-only facts about the app, exposed to QML as the singleton `AppInfo`.
// Also proves the link from QML -> app -> core (version comes from mp::core).
class AppInfo : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString sampleDuration READ sampleDuration CONSTANT)

public:
    explicit AppInfo(QObject* parent = nullptr);

    QString name() const;
    QString version() const;
    // Runs core::formatDuration on a fixed value, to show core code reaching the UI.
    QString sampleDuration() const;
};

}  // namespace mp::app

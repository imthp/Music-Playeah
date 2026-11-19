#pragma once

#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <initializer_list>
#include <string>
#include <utility>

// Small helpers shared by the library controller's .cpp files (query results -> QML values).
namespace mp::app {

inline QString fromUtf8(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

inline QVariantMap map(std::initializer_list<std::pair<const char*, QVariant>> items) {
    QVariantMap m;
    for (const auto& [key, value] : items) {
        m.insert(QString::fromLatin1(key), value);
    }
    return m;
}

}  // namespace mp::app

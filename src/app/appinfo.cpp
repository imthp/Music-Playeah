#include "appinfo.hpp"

#include "core/format.hpp"
#include "core/version.hpp"

namespace mp::app {

namespace {
QString fromView(std::string_view s) {
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size()));
}
}  // namespace

AppInfo::AppInfo(QObject* parent) : QObject(parent) {}

QString AppInfo::name() const {
    return fromView(core::kAppName);
}

QString AppInfo::version() const {
    return fromView(core::kVersion);
}

QString AppInfo::sampleDuration() const {
    return QString::fromStdString(core::formatDuration(3'723'000));
}

}  // namespace mp::app

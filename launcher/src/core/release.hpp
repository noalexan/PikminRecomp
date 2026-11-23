// GitHub "latest release" JSON -> what the launcher needs for its platform.
#pragma once
#include <QByteArray>
#include <QString>
#include <optional>

namespace launcher {

struct Release {
    QString version;   // tag without the leading "v"
    QString assetName, assetUrl;
    qint64 assetSize = 0;
    QString sha256;    // from a "sha256 <asset name> <hex>" line in the release body ("" if none)
};

// platform: "linux" (asset ending -x86_64.AppImage) or "windows" (-windows.zip).
std::optional<Release> parse_release(const QByteArray& json, const QString& platform);
QString source_archive_url(const QString& version, bool zip);

}  // namespace launcher

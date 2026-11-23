#include "core/release.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

namespace launcher {

std::optional<Release> parse_release(const QByteArray& json, const QString& platform) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return std::nullopt;
    const QJsonObject o = doc.object();
    QString tag = o.value("tag_name").toString();
    if (tag.isEmpty()) return std::nullopt;
    Release r;
    r.version = tag.startsWith('v') ? tag.mid(1) : tag;
    const QString suffix = platform == "windows" ? "-windows.zip" : "-x86_64.AppImage";
    for (const auto& v : o.value("assets").toArray()) {
        const QJsonObject a = v.toObject();
        if (!a.value("name").toString().endsWith(suffix)) continue;
        r.assetName = a.value("name").toString();
        r.assetUrl = a.value("browser_download_url").toString();
        r.assetSize = a.value("size").toInteger();
    }
    for (const QString& line : o.value("body").toString().split('\n')) {
        const QStringList w = line.trimmed().split(' ', Qt::SkipEmptyParts);
        if (w.size() == 3 && w[0] == "sha256" && w[1] == r.assetName) r.sha256 = w[2].toLower();
    }
    return r;
}

QString source_archive_url(const QString& version, bool zip) {
    return "https://github.com/noalexan/PikminRecomp/archive/refs/tags/v" + version + (zip ? ".zip" : ".tar.gz");
}

}  // namespace launcher

#include "core/release.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "core/version.hpp"

namespace launcher {

static std::optional<Release> parse_one(const QJsonObject& o, const QString& platform) {
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

std::optional<Release> parse_release(const QByteArray& json, const QString& platform) {
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (doc.isObject()) return parse_one(doc.object(), platform);
    if (!doc.isArray()) return std::nullopt;
    // The release list: the newest non-draft release (prereleases included) with an asset for this
    // platform, else the newest one at all ("up to date" when it has nothing to download).
    std::optional<Release> best, best_any;
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        if (o.value("draft").toBool()) continue;
        const auto r = parse_one(o, platform);
        if (!r) continue;
        if (!best_any || compare_versions(r->version, best_any->version) > 0) best_any = r;
        if (!r->assetUrl.isEmpty() && (!best || compare_versions(r->version, best->version) > 0)) best = r;
    }
    if (best) return best;
    if (best_any) return best_any;
    return Release{};  // no release published yet
}

QString source_archive_url(const QString& version, bool zip) {
    return "https://github.com/noalexan/PikminRecomp/archive/refs/tags/v" + version + (zip ? ".zip" : ".tar.gz");
}

}  // namespace launcher

#include "core/rom.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace launcher {
namespace {
uint32_t be32(const QByteArray& b, int at) {
    return uint32_t(uint8_t(b[at])) << 24 | uint32_t(uint8_t(b[at + 1])) << 16 | uint32_t(uint8_t(b[at + 2])) << 8 |
           uint8_t(b[at + 3]);
}
RomResult refuse(QString msg, QString id = {}) {
    RomResult r;
    r.gameId = std::move(id);
    r.message = std::move(msg);
    return r;
}
}  // namespace

RomResult check_rom(const QString& path) {
    const QFileInfo info(path);
    if (info.isDir()) return refuse("That's a folder. Choose the disc image file (.iso or .gcm).");
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return refuse("Can't open this file.");
    const QByteArray head = f.read(0x440);
    if (head.startsWith("RVZ\x01") || head.startsWith("WIA\x01"))
        return refuse("This is a compressed RVZ/WIA image. Convert it to a plain ISO first (Dolphin: right-click > Convert File).");
    if (head.startsWith("CISO")) return refuse("This is a CISO image. Convert it to a plain ISO first.");
    if (head.size() < 0x440) return refuse("This file is too small to be a GameCube disc image.");
    if (head.size() >= 0x204 && head.mid(0x200, 4) == "NKIT")
        return refuse("This is an NKit image. Restore it to a full ISO first (NKit: Recover to ISO).");
    if (be32(head, 0x1C) != 0xC2339F3Du) return refuse("This isn't a GameCube disc image.");
    const QString id = QString::fromLatin1(head.left(6));
    if (id == "GPIE01") return refuse("This is the NTSC (US) Pikmin. Pikmin Recomp is built for the PAL (European) disc, GPIP01.", id);
    if (id == "GPIJ01") return refuse("This is the Japanese Pikmin. Pikmin Recomp is built for the PAL (European) disc, GPIP01.", id);
    if (id != "GPIP01") return refuse("This isn't Pikmin (game ID " + id + "). Pikmin Recomp needs the PAL disc, GPIP01.", id);

    const uint32_t dol = be32(head, 0x420), fst = be32(head, 0x424), fst_size = be32(head, 0x428);
    const qint64 size = f.size();
    if (dol + 0x100ull > uint64_t(size) || fst + uint64_t(fst_size) > uint64_t(size))
        return refuse("This disc image is truncated (incomplete dump).", id);
    f.seek(dol);
    const QByteArray dol_head = f.read(0x100);
    uint64_t dol_size = 0x100;
    for (int i = 0; i < 18; ++i) {
        const uint32_t off = be32(dol_head, i * 4), sz = be32(dol_head, 0x90 + i * 4);
        if (sz) dol_size = std::max<uint64_t>(dol_size, uint64_t(off) + sz);
    }
    if (dol + dol_size > uint64_t(size)) return refuse("This disc image is truncated (incomplete dump).", id);
    f.seek(dol);
    RomResult r;
    r.gameId = id;
    r.dolSha1 = QString::fromLatin1(QCryptographicHash::hash(f.read(qint64(dol_size)), QCryptographicHash::Sha1).toHex());
    f.seek(fst);
    r.fstSha1 = QString::fromLatin1(QCryptographicHash::hash(f.read(fst_size), QCryptographicHash::Sha1).toHex());
    if (r.dolSha1 != kDolSha1 || r.fstSha1 != kFstSha1) {
        r.message = "This Pikmin disc differs from the one Pikmin Recomp is built for (modified or another revision).";
        return r;
    }
    r.ok = true;
    r.message = "Pikmin · PAL (GPIP01) · verified";
    return r;
}

}  // namespace launcher

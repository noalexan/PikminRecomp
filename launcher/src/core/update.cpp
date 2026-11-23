#include "core/update.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <utility>

namespace launcher {

bool swap_in_tree(const QString& staged, const QString& target, QString* error) {
    const QDir src(staged);
    // (new file now at `path`, the old one moved to `path.old` or "" if there was none)
    QList<std::pair<QString, QString>> done;
    auto rollback = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            QFile::remove(it->first);
            if (!it->second.isEmpty()) QFile::rename(it->second, it->first);
        }
    };
    QDirIterator it(staged, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString from = it.next();
        const QString to = target + "/" + src.relativeFilePath(from);
        QDir().mkpath(QFileInfo(to).absolutePath());
        QString old;
        if (QFileInfo::exists(to)) {
            old = to + ".old";
            QFile::remove(old);  // a leftover from an earlier update
            if (!QFile::rename(to, old)) {
                rollback();
                if (error) *error = "Can't move aside " + QDir::toNativeSeparators(to);
                return false;
            }
        }
        // QFile::rename falls back to copy + remove across volumes (data dir vs launcher folder).
        if (!QFile::rename(from, to)) {
            if (!old.isEmpty()) QFile::rename(old, to);
            rollback();
            if (error) *error = "Can't write " + QDir::toNativeSeparators(to);
            return false;
        }
        done.append({to, old});
    }
    if (error) error->clear();
    return true;
}

void remove_old_files(const QString& dir) {
    QDirIterator it(dir, {"*.old"}, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) QFile::remove(it.next());
}

}  // namespace launcher

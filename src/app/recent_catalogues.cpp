#include "app/recent_catalogues.h"

#include <QFileInfo>
#include <QSettings>

namespace pinax::app {

namespace {

const QString recentKey = QStringLiteral("catalogue/recent");

QString canonical(const QString& path)
{
    const QFileInfo file(path);
    return file.exists() ? file.canonicalFilePath() : file.absoluteFilePath();
}

} // namespace

RecentCatalogues::RecentCatalogues(QSettings& settings)
    : settings_(settings)
{
}

QStringList RecentCatalogues::list() const
{
    QStringList present;
    for (const QString& path : settings_.value(recentKey).toStringList()) {
        if (QFileInfo::exists(path))
            present << path;
    }
    return present;
}

QString RecentCatalogues::last() const
{
    const QStringList present = list();
    return present.isEmpty() ? QString() : present.front();
}

void RecentCatalogues::remember(const QString& path)
{
    const QString file = canonical(path);
    QStringList recent = settings_.value(recentKey).toStringList();
    recent.removeAll(file);
    recent.prepend(file);
    while (recent.size() > kept)
        recent.removeLast();
    settings_.setValue(recentKey, recent);
    settings_.sync();
}

void RecentCatalogues::forget(const QString& path)
{
    QStringList recent = settings_.value(recentKey).toStringList();
    recent.removeAll(canonical(path));
    settings_.setValue(recentKey, recent);
    settings_.sync();
}

} // namespace pinax::app

#pragma once

#include <QString>
#include <QStringList>

class QSettings;

namespace pinax::app {

// The catalogues the owner has had open, most recent first (F-026, D-028),
// kept in Pinax's settings file: the one to reopen at launch and the list
// under File ▸ Open Recent.
class RecentCatalogues {
public:
    static constexpr int kept = 8;

    explicit RecentCatalogues(QSettings& settings);

    // Most recent first; files that have since gone are left out.
    QStringList list() const;
    // The catalogue to reopen at launch, if it is still there.
    QString last() const;

    void remember(const QString& path);
    void forget(const QString& path);

private:
    QSettings& settings_;
};

} // namespace pinax::app

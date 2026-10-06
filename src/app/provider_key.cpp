#include "app/provider_key.h"

#include <QDir>
#include <QFile>

namespace pinax::app {

QString findGoogleBooksKey(const QStringList& directories)
{
    const QString fromEnvironment = qEnvironmentVariable("PINAX_GOOGLE_BOOKS_KEY").trimmed();
    if (!fromEnvironment.isEmpty())
        return fromEnvironment;
    for (const QString& directory : directories) {
        QFile file(QDir(directory).filePath(QStringLiteral("google-books.key")));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString key = QString::fromUtf8(file.readAll()).trimmed();
        if (!key.isEmpty())
            return key;
    }
    return {};
}

} // namespace pinax::app

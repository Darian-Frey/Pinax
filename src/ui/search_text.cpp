#include "ui/search_text.h"

namespace pinax::ui {

QString searchKey(const QString& text)
{
    // Decomposed, an accented letter is the letter and its mark.
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString key;
    key.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if (c == QLatin1Char('\'') || c == QChar(0x2019)) // ' and ’
            continue;
        if (c.isLetterOrNumber())
            key += c.toLower();
        else if (!key.endsWith(QLatin1Char(' ')))
            key += QLatin1Char(' ');
    }
    // Letters with no decomposition: ł, ø, đ, ß and the like.
    key.replace(QChar(0x0142), QLatin1Char('l'));
    key.replace(QChar(0x00F8), QLatin1Char('o'));
    key.replace(QChar(0x0111), QLatin1Char('d'));
    key.replace(QChar(0x00DF), QStringLiteral("ss"));
    return key.trimmed();
}

QStringList searchWords(const QString& text)
{
    return searchKey(text).split(QLatin1Char(' '), Qt::SkipEmptyParts);
}

} // namespace pinax::ui

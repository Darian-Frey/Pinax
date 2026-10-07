#pragma once

#include <QString>
#include <QStringList>

namespace pinax::ui {

// Text as search compares it (F-019): lower case, accents dropped,
// apostrophes removed, any other punctuation a space — so "lem" finds
// "Stanisław Lem" and "hitchhikers" finds "The Hitchhiker's Guide".
QString searchKey(const QString& text);

// The words of a search, each normalised as searchKey; empty for blank text.
QStringList searchWords(const QString& text);

} // namespace pinax::ui

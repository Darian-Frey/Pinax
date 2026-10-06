#pragma once

#include <QString>
#include <QStringList>

namespace pinax::app {

// The Google Books API key (D-021), never compiled in or committed: the
// PINAX_GOOGLE_BOOKS_KEY environment variable if set, else the first
// `google-books.key` found in these directories, in order. Surrounding
// whitespace is dropped. Empty when there is none, and Google is not asked.
QString findGoogleBooksKey(const QStringList& directories);

} // namespace pinax::app

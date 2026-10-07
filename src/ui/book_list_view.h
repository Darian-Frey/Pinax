#pragma once

#include "domain/book_summary.h"

#include <QList>
#include <QTableView>

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pinax::ui {

class BookGroupProxy;
class BookListModel;
class BookSortProxy;
enum class Grouping;

// The middle panel: every book, sortable by clicking a column header
// (D-010). Opens sorted by author, as in the mock-up.
class BookListView : public QTableView {
    Q_OBJECT

public:
    explicit BookListView(QWidget* parent = nullptr);

    void setBooks(std::vector<domain::BookSummary> books);

    // Refreshes one row in place; selection and sort position follow it.
    void updateBook(const domain::BookSummary& summary);

    // Selects the book and scrolls to it, if it is listed.
    void selectBook(std::int64_t id);

    // Shows only these books, or every book for nullopt. The set stays as
    // given until the next call: a book that stops matching is not snatched
    // away from under the owner.
    void showOnly(const std::optional<QList<qint64>>& ids);

    // Header rows for each series, author or genre (F-018); `genres` maps
    // each book to its genres, for grouping by genre. Grouping by series
    // sorts by series, so each group runs in position order.
    void setGrouping(Grouping grouping, std::map<std::int64_t, std::vector<std::string>> genres = {});
    Grouping grouping() const;

    // Search (F-019): what each book is found by, and the words to find.
    void setSearchTexts(const std::map<std::int64_t, std::string>& texts);
    void search(const QString& text);

    // How many books are listed under the current filter.
    int shownCount() const;

    // Ids of the books listed now — filters, search and sort applied — in
    // display order, each once however many groups show it (F-023).
    QList<qint64> shownBooks() const;

    // Ids of the selected books, in display order.
    QList<qint64> selectedBooks() const;

    BookListModel* bookModel() const { return model_; }
    BookSortProxy* sortProxy() const { return proxy_; }
    BookGroupProxy* groupProxy() const { return groups_; }

signals:
    void selectionChangedTo(const QList<qint64>& ids);

    // R with books selected (F-005).
    void toggleReadRequested(const QList<qint64>& ids);

    // 1-9 rate, 0 rates 10, Backspace or - clears (sent as 0) (F-007).
    void ratingRequested(const QList<qint64>& ids, int rating);

    // Delete with books selected (F-001). Asks; deletes nothing itself.
    void deleteRequested(const QList<qint64>& ids);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    BookListModel* model_;
    BookSortProxy* proxy_;
    BookGroupProxy* groups_;

    // The book a row of the view shows, if it shows one.
    std::optional<std::int64_t> bookAt(const QModelIndex& index) const;
    void layHeaderSpans();
};

} // namespace pinax::ui

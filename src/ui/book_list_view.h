#pragma once

#include "domain/book_summary.h"

#include <QList>
#include <QTableView>

#include <vector>

namespace pinax::ui {

class BookListModel;
class BookSortProxy;

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

    // Ids of the selected books, in display order.
    QList<qint64> selectedBooks() const;

    BookListModel* bookModel() const { return model_; }
    BookSortProxy* sortProxy() const { return proxy_; }

signals:
    void selectionChangedTo(const QList<qint64>& ids);

private:
    BookListModel* model_;
    BookSortProxy* proxy_;
};

} // namespace pinax::ui

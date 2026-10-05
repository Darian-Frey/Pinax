#pragma once

#include "domain/book_summary.h"

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

    BookListModel* bookModel() const { return model_; }
    BookSortProxy* sortProxy() const { return proxy_; }

private:
    BookListModel* model_;
    BookSortProxy* proxy_;
};

} // namespace pinax::ui

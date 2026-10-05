#pragma once

#include "domain/book_summary.h"

#include <QAbstractTableModel>

#include <vector>

namespace pinax::ui {

// The catalogue as a table, one row per book. Fed with summaries read from
// v_book_display by the caller; issues no SQL (ARCHITECTURE.md invariant 8).
class BookListModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        ReadColumn,
        TitleColumn,
        AuthorColumn,
        SeriesColumn,
        RatingColumn,
        YearColumn,
        TimesReadColumn, // F-006
        ColumnCount
    };

    explicit BookListModel(QObject* parent = nullptr);

    void setBooks(std::vector<domain::BookSummary> books);
    const domain::BookSummary& book(int row) const;

    // Replaces the row with this summary's id, if there is one.
    void updateBook(const domain::BookSummary& summary);

    // The row holding this book, or -1.
    int rowOf(std::int64_t id) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
        int role = Qt::DisplayRole) const override;

private:
    std::vector<domain::BookSummary> books_;
};

} // namespace pinax::ui

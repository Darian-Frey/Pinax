#pragma once

#include "domain/book_query.h"
#include "ui/book_group_proxy.h"

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace pinax::ui {

// The book list's filters, combined (F-017): a search across title, author
// and series (F-019), read state, rating (unrated, or a range), genre,
// author and series. Changing any emits the whole query;
// the caller narrows the list. Clear filters resets them all in one action.
// Fed its choices; issues no SQL.
class FilterBar : public QWidget {
    Q_OBJECT

public:
    explicit FilterBar(QWidget* parent = nullptr);

    // The choices, each with its count. A choice still offered stays chosen.
    void setOptions(const std::vector<domain::FilterOption>& genres,
        const std::vector<domain::FilterOption>& authors, const std::vector<domain::FilterOption>& series);

    // Shows this query without emitting.
    void setQuery(const domain::BookQuery& query);
    domain::BookQuery query() const;

    // Puts the cursor in the search field, its text selected.
    void focusSearch();

    // How the list is grouped (F-018). Not a filter: Clear filters keeps it.
    Grouping grouping() const;

    // "23 of 443 shown", or nothing when no filter is set.
    void setShown(int shown, int total);

signals:
    void queryChanged(const domain::BookQuery& query);
    void groupingChanged(pinax::ui::Grouping grouping);

private:
    void changed();
    void updateClear();

    QLineEdit* search_;
    QComboBox* readState_;
    QComboBox* rating_;
    QComboBox* genre_;
    QComboBox* author_;
    QComboBox* series_;
    QComboBox* groupBy_;
    QWidget* range_;
    QSpinBox* ratingFrom_;
    QSpinBox* ratingTo_;
    QLabel* shown_;
    QPushButton* clear_;
    bool quiet_ = false;
};

} // namespace pinax::ui

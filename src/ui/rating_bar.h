#pragma once

#include <QWidget>

#include <optional>

namespace pinax::ui {

// Ten squares, filled to the rating, as in the mock-up. Display only; the
// rating control of F-007 builds on it.
class RatingBar : public QWidget {
    Q_OBJECT

public:
    explicit RatingBar(QWidget* parent = nullptr);

    void setRating(std::optional<int> rating);
    std::optional<int> rating() const { return rating_; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    std::optional<int> rating_;
};

} // namespace pinax::ui

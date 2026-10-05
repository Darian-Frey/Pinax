#pragma once

#include <QWidget>

#include <optional>

namespace pinax::ui {

// Ten squares, filled to the rating, as in the mock-up. When interactive,
// clicking the nth square chooses n, and clicking the square of the rating
// already shown clears it (F-007); hovering previews the choice.
class RatingBar : public QWidget {
    Q_OBJECT

public:
    explicit RatingBar(QWidget* parent = nullptr);

    void setRating(std::optional<int> rating);
    std::optional<int> rating() const { return rating_; }

    void setInteractive(bool interactive);

    // The rating a click at this point would choose: 1-10, or 0 to clear.
    // -1 outside the squares.
    int ratingAt(const QPoint& point) const;

    QSize sizeHint() const override;

signals:
    // 1-10, or 0 for unrated.
    void ratingChosen(int rating);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    std::optional<int> rating_;
    bool interactive_ = false;
    int hover_ = 0; // square under the pointer, 1-10; 0 for none
};

} // namespace pinax::ui

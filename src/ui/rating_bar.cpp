#include "ui/rating_bar.h"

#include "ui/style.h"

#include <QPainter>

namespace pinax::ui {

namespace {

constexpr int squares = 10;
constexpr int squareSize = 10;
constexpr int gap = 2;

} // namespace

RatingBar::RatingBar(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void RatingBar::setRating(std::optional<int> rating)
{
    rating_ = rating;
    setToolTip(rating ? tr("Rated %1 out of 10").arg(*rating) : tr("Unrated"));
    update();
}

QSize RatingBar::sizeHint() const
{
    return QSize(squares * squareSize + (squares - 1) * gap, squareSize);
}

void RatingBar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int filled = rating_.value_or(0);
    const QColor outline = muted(this);
    for (int i = 0; i < squares; ++i) {
        const QRectF square(i * (squareSize + gap) + 0.5, 0.5, squareSize - 1, squareSize - 1);
        if (i < filled) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent());
        } else {
            painter.setPen(QPen(outline, 1));
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawRoundedRect(square, 2, 2);
    }
}

} // namespace pinax::ui

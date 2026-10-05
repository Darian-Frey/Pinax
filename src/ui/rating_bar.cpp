#include "ui/rating_bar.h"

#include "ui/style.h"

#include <QMouseEvent>
#include <QPainter>

namespace pinax::ui {

namespace {

constexpr int squares = 10;
constexpr int squareSize = 9;
constexpr int gap = 2;

} // namespace

RatingBar::RatingBar(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(sizeHint()); // never squeezed: a clipped square reads as a lower rating
}

void RatingBar::setRating(std::optional<int> rating)
{
    rating_ = rating;
    QString tip = rating ? tr("Rated %1 out of 10").arg(*rating) : tr("Unrated");
    if (interactive_)
        tip += tr(" — click to rate, click the same square again to clear");
    setToolTip(tip);
    update();
}

void RatingBar::setInteractive(bool interactive)
{
    interactive_ = interactive;
    setMouseTracking(interactive);
    setCursor(interactive ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setRating(rating_);
}

int RatingBar::ratingAt(const QPoint& point) const
{
    if (point.y() < 0 || point.y() >= squareSize || point.x() < 0)
        return -1;
    const int square = point.x() / (squareSize + gap) + 1;
    if (square > squares)
        return -1;
    return rating_ == square ? 0 : square;
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
    QColor preview = accent();
    preview.setAlphaF(0.45f);

    for (int i = 0; i < squares; ++i) {
        const QRectF square(i * (squareSize + gap) + 0.5, 0.5, squareSize - 1, squareSize - 1);
        const bool previewed = hover_ > 0 && i < hover_;
        if (previewed) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(i < filled ? accent() : preview);
        } else if (hover_ == 0 && i < filled) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(accent());
        } else {
            painter.setPen(QPen(outline, 1));
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawRoundedRect(square, 2, 2);
    }
}

void RatingBar::mouseMoveEvent(QMouseEvent* event)
{
    if (!interactive_)
        return;
    const int at = ratingAt(event->position().toPoint());
    const int square = at < 0 ? 0 : (at == 0 ? rating_.value_or(0) : at);
    if (square != hover_) {
        hover_ = square;
        update();
    }
}

void RatingBar::mousePressEvent(QMouseEvent* event)
{
    if (!interactive_ || event->button() != Qt::LeftButton)
        return;
    const int chosen = ratingAt(event->position().toPoint());
    if (chosen >= 0) {
        hover_ = 0;
        emit ratingChosen(chosen);
    }
}

void RatingBar::leaveEvent(QEvent*)
{
    hover_ = 0;
    update();
}

} // namespace pinax::ui

#include "ui/filter_bar.h"

#include "ui/style.h"

#include <QComboBox>
#include <QCompleter>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace pinax::ui {

using domain::BookQuery;
using domain::FilterOption;
using domain::ReadStatus;

namespace {

enum RatingChoice { AnyRating, Unrated, RatedRange };

QComboBox* makeChoice(const QString& name, QWidget* parent)
{
    auto* combo = new QComboBox(parent);
    combo->setObjectName(name);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(9);
    return combo;
}

// A long list to type into: "Banks" finds "Iain M. Banks (12)".
QComboBox* makeSearchable(const QString& name, QWidget* parent)
{
    QComboBox* combo = makeChoice(name, parent);
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    combo->completer()->setFilterMode(Qt::MatchContains);
    combo->completer()->setCompletionMode(QCompleter::PopupCompletion);
    combo->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    return combo;
}

std::optional<std::int64_t> chosenId(const QComboBox* combo)
{
    if (combo->currentIndex() <= 0)
        return std::nullopt;
    return combo->currentData().toLongLong();
}

void fill(QComboBox* combo, const QString& any, const std::vector<FilterOption>& options)
{
    const auto keep = chosenId(combo);
    combo->clear();
    combo->addItem(any);
    for (const auto& option : options) {
        combo->addItem(QStringLiteral("%1 (%2)").arg(QString::fromStdString(option.name)).arg(option.count),
            QVariant::fromValue<qlonglong>(option.id));
    }
    const int index = keep ? combo->findData(QVariant::fromValue<qlonglong>(*keep)) : 0;
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

void choose(QComboBox* combo, const std::optional<std::int64_t>& id)
{
    const int index = id ? combo->findData(QVariant::fromValue<qlonglong>(*id)) : 0;
    combo->setCurrentIndex(index < 0 ? 0 : index);
}

} // namespace

FilterBar::FilterBar(QWidget* parent)
    : QWidget(parent)
    , readState_(makeChoice(QStringLiteral("filter.read"), this))
    , rating_(makeChoice(QStringLiteral("filter.rating"), this))
    , genre_(makeSearchable(QStringLiteral("filter.genre"), this))
    , author_(makeSearchable(QStringLiteral("filter.author"), this))
    , series_(makeSearchable(QStringLiteral("filter.series"), this))
    , groupBy_(makeChoice(QStringLiteral("filter.groupBy"), this))
    , range_(new QWidget(this))
    , ratingFrom_(new QSpinBox(range_))
    , ratingTo_(new QSpinBox(range_))
    , shown_(new QLabel(this))
    , clear_(new QPushButton(tr("Clear filters"), this))
{
    readState_->addItem(tr("Any read state"));
    readState_->addItem(tr("Unread"), static_cast<int>(ReadStatus::Unread));
    readState_->addItem(tr("Reading"), static_cast<int>(ReadStatus::Reading));
    readState_->addItem(tr("Read"), static_cast<int>(ReadStatus::Read));
    readState_->addItem(tr("Abandoned"), static_cast<int>(ReadStatus::Abandoned));
    groupBy_->addItem(tr("No grouping"), static_cast<int>(Grouping::None));
    groupBy_->addItem(tr("Group by series"), static_cast<int>(Grouping::Series));
    groupBy_->addItem(tr("Group by author"), static_cast<int>(Grouping::Author));
    groupBy_->addItem(tr("Group by genre"), static_cast<int>(Grouping::Genre));
    groupBy_->setMinimumContentsLength(16);
    groupBy_->setToolTip(tr("Headings for each series, author or genre, with how many books each holds"));
    rating_->addItem(tr("Any rating"));
    rating_->addItem(tr("Unrated"));
    rating_->addItem(tr("Rated…"));
    fill(genre_, tr("Any genre"), {});
    fill(author_, tr("Any author"), {});
    fill(series_, tr("Any series"), {});
    genre_->setToolTip(tr("Genres as the providers name them; type to narrow"));
    author_->setToolTip(tr("Authors credited as author, not as editor; type to narrow"));

    auto* rangeLayout = new QHBoxLayout(range_);
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    ratingFrom_->setObjectName(QStringLiteral("filter.ratingFrom"));
    ratingTo_->setObjectName(QStringLiteral("filter.ratingTo"));
    for (QSpinBox* box : {ratingFrom_, ratingTo_})
        box->setRange(1, 10);
    ratingFrom_->setValue(1);
    ratingTo_->setValue(10);
    rangeLayout->addWidget(new QLabel(tr("Rated from"), range_));
    rangeLayout->addWidget(ratingFrom_);
    rangeLayout->addWidget(new QLabel(tr("to"), range_));
    rangeLayout->addWidget(ratingTo_);
    range_->hide();

    shown_->setObjectName(QStringLiteral("filter.shown"));
    QPalette palette = shown_->palette();
    palette.setColor(QPalette::WindowText, muted(shown_));
    shown_->setPalette(palette);
    clear_->setObjectName(QStringLiteral("filter.clear"));
    clear_->setToolTip(tr("Show every book again"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 4);
    layout->setSpacing(4);
    auto* choices = new QHBoxLayout;
    for (QComboBox* combo : {readState_, rating_, genre_, author_, series_})
        choices->addWidget(combo, 1);
    layout->addLayout(choices);
    auto* status = new QHBoxLayout;
    status->addWidget(groupBy_);
    status->addWidget(range_);
    status->addStretch();
    status->addWidget(shown_);
    status->addWidget(clear_);
    layout->addLayout(status);

    for (QComboBox* combo : {readState_, rating_, genre_, author_, series_})
        connect(combo, &QComboBox::currentIndexChanged, this, &FilterBar::changed);
    // Typed text that matches nothing reverts to the choice it had.
    for (QComboBox* combo : {genre_, author_, series_}) {
        connect(combo->lineEdit(), &QLineEdit::editingFinished, this,
            [combo] { combo->setEditText(combo->itemText(combo->currentIndex())); });
    }
    connect(groupBy_, &QComboBox::currentIndexChanged, this, [this] { emit groupingChanged(grouping()); });
    connect(ratingFrom_, &QSpinBox::valueChanged, this, [this](int value) {
        if (ratingTo_->value() < value)
            ratingTo_->setValue(value);
        changed();
    });
    connect(ratingTo_, &QSpinBox::valueChanged, this, [this](int value) {
        if (ratingFrom_->value() > value)
            ratingFrom_->setValue(value);
        changed();
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        setQuery({});
        emit queryChanged({});
    });
    updateClear();
}

void FilterBar::setOptions(const std::vector<FilterOption>& genres, const std::vector<FilterOption>& authors,
    const std::vector<FilterOption>& series)
{
    quiet_ = true;
    fill(genre_, tr("Any genre"), genres);
    fill(author_, tr("Any author"), authors);
    fill(series_, tr("Any series"), series);
    quiet_ = false;
}

void FilterBar::setQuery(const BookQuery& query)
{
    quiet_ = true;
    readState_->setCurrentIndex(query.readStatus ? readState_->findData(static_cast<int>(*query.readStatus)) : 0);
    if (query.unratedOnly) {
        rating_->setCurrentIndex(Unrated);
    } else if (query.ratingFrom || query.ratingTo) {
        rating_->setCurrentIndex(RatedRange);
        ratingFrom_->setValue(query.ratingFrom.value_or(1));
        ratingTo_->setValue(query.ratingTo.value_or(10));
    } else {
        rating_->setCurrentIndex(AnyRating);
    }
    range_->setVisible(rating_->currentIndex() == RatedRange);
    choose(genre_, query.genreId);
    choose(author_, query.authorId);
    choose(series_, query.seriesId);
    quiet_ = false;
    updateClear();
}

BookQuery FilterBar::query() const
{
    BookQuery query;
    if (readState_->currentIndex() > 0)
        query.readStatus = static_cast<ReadStatus>(readState_->currentData().toInt());
    if (rating_->currentIndex() == Unrated) {
        query.unratedOnly = true;
    } else if (rating_->currentIndex() == RatedRange) {
        query.ratingFrom = ratingFrom_->value();
        query.ratingTo = ratingTo_->value();
    }
    query.genreId = chosenId(genre_);
    query.authorId = chosenId(author_);
    query.seriesId = chosenId(series_);
    return query;
}

Grouping FilterBar::grouping() const
{
    return static_cast<Grouping>(groupBy_->currentData().toInt());
}

void FilterBar::setShown(int shown, int total)
{
    shown_->setText(query().empty() ? QString() : tr("%1 of %2 shown").arg(shown).arg(total));
}

void FilterBar::changed()
{
    range_->setVisible(rating_->currentIndex() == RatedRange);
    updateClear();
    if (!quiet_)
        emit queryChanged(query());
}

void FilterBar::updateClear()
{
    clear_->setEnabled(!query().empty());
}

} // namespace pinax::ui

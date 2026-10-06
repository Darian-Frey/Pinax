#include "ui/series_view.h"

#include "ui/style.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace pinax::ui {

using domain::MissingVolume;
using domain::SeriesDetail;

namespace {

constexpr int shownMissing = 8;

QFrame* makeRule(QWidget* parent)
{
    auto* rule = new QFrame(parent);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Plain);
    return rule;
}

QLabel* makeValue(const QString& name, QWidget* parent)
{
    auto* label = new QLabel(parent);
    label->setObjectName(name);
    label->setWordWrap(true);
    return label;
}

QString volumeName(const MissingVolume& volume)
{
    QString name = volume.title ? QString::fromStdString(*volume.title).toHtmlEscaped()
                                : SeriesView::tr("Untitled volume");
    if (volume.position)
        name = SeriesView::tr("%1 <span style='opacity:0.6'>· %2</span>")
                   .arg(name, QString::fromStdString(*volume.position).toHtmlEscaped());
    return name;
}

QString lengthText(const domain::SeriesStatus& series)
{
    QString text = series.known == 1 ? SeriesView::tr("1 volume")
                                     : SeriesView::tr("%1 volumes").arg(series.known);
    // Only what is recorded: a series not flagged as ongoing is not thereby
    // known to be finished, so "concluded" is never claimed.
    if (series.ongoing)
        text += SeriesView::tr(" · still being written");
    return text;
}

} // namespace

SeriesView::SeriesView(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);

    layout->addWidget(makeSectionHeading(tr("Series"), this));
    name_ = makeValue(QStringLiteral("seriesView.name"), this);
    QFont nameFont = name_->font();
    nameFont.setPointSizeF(nameFont.pointSizeF() * 1.45);
    nameFont.setBold(true);
    name_->setFont(nameFont);
    layout->addWidget(name_);
    byline_ = makeValue(QStringLiteral("seriesView.byline"), this);
    layout->addWidget(byline_);

    auto* progressRow = new QHBoxLayout;
    progress_ = new QProgressBar(this);
    progress_->setObjectName(QStringLiteral("seriesView.progress"));
    progress_->setTextVisible(false);
    progress_->setFixedHeight(5);
    progress_->setStyleSheet(QStringLiteral(
        "QProgressBar { border: none; background: %1; border-radius: 2px; }"
        "QProgressBar::chunk { background: %2; border-radius: 2px; }")
            .arg(muted(this).darker(200).name(QColor::HexArgb), accent().name()));
    held_ = makeValue(QStringLiteral("seriesView.held"), this);
    held_->setWordWrap(false);
    progressRow->addWidget(progress_, 1);
    progressRow->addWidget(held_);
    layout->addLayout(progressRow);
    legend_ = makeValue(QStringLiteral("seriesView.legend"), this);
    layout->addWidget(legend_);

    // The volume chosen on the series page, with what can be done to it.
    card_ = new QWidget(this);
    card_->setObjectName(QStringLiteral("seriesView.card"));
    auto* cardLayout = new QVBoxLayout(card_);
    cardLayout->setContentsMargins(0, 6, 0, 0);
    cardLayout->addWidget(makeSectionHeading(tr("Selected volume"), card_));
    cardTitle_ = makeValue(QStringLiteral("seriesView.cardTitle"), card_);
    cardTitle_->setTextFormat(Qt::RichText);
    cardLayout->addWidget(cardTitle_);
    auto* cardButtons = new QHBoxLayout;
    markOwned_ = new QPushButton(tr("Mark as owned"), card_);
    markOwned_->setObjectName(QStringLiteral("seriesView.markOwned"));
    auto* editEntry = new QPushButton(tr("Edit entry"), card_);
    editEntry->setObjectName(QStringLiteral("seriesView.editEntry"));
    editEntry->setToolTip(tr("Edit this volume's position and title (F2)"));
    cardButtons->addWidget(markOwned_);
    cardButtons->addWidget(editEntry);
    cardButtons->addStretch();
    cardLayout->addLayout(cardButtons);
    layout->addWidget(card_);
    card_->hide();
    connect(markOwned_, &QPushButton::clicked, this, [this] {
        if (selectedEntry_)
            emit markOwnedRequested(*selectedEntry_);
    });
    connect(editEntry, &QPushButton::clicked, this, [this] {
        if (selectedEntry_)
            emit editEntryRequested(*selectedEntry_);
    });

    layout->addWidget(makeRule(this));
    missingHeading_ = makeSectionHeading(tr("Missing"), this);
    missingHeading_->setObjectName(QStringLiteral("seriesView.missingHeading"));
    layout->addWidget(missingHeading_);
    missing_ = makeValue(QStringLiteral("seriesView.missing"), this);
    missing_->setTextFormat(Qt::RichText);
    layout->addWidget(missing_);

    layout->addWidget(makeRule(this));
    layout->addWidget(makeSectionHeading(tr("Across the library"), this));
    auto* totals = new QGridLayout;
    totals->setColumnStretch(0, 1);
    auto addTotal = [&](const QString& label, const QString& name) {
        const int row = totals->rowCount();
        totals->addWidget(new QLabel(label, this), row, 0);
        QLabel* value = makeValue(name, this);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        totals->addWidget(value, row, 1);
        return value;
    };
    oneShort_ = addTotal(tr("Series one volume short"), QStringLiteral("seriesView.oneShort"));
    complete_ = addTotal(tr("Series complete"), QStringLiteral("seriesView.complete"));
    withGaps_ = addTotal(tr("Series with gaps"), QStringLiteral("seriesView.withGaps"));
    notOwned_ = addTotal(tr("Volumes not owned"), QStringLiteral("seriesView.notOwned"));
    layout->addLayout(totals);
    auto* note = new QLabel(tr("Counts come from the series views, not a stored status, so they "
                               "move the moment a volume is added."),
        this);
    note->setWordWrap(true);
    QPalette palette = note->palette();
    palette.setColor(QPalette::WindowText, muted(note));
    note->setPalette(palette);
    layout->addWidget(note);
    layout->addStretch();
}

void SeriesView::showSeries(const SeriesDetail& detail, const std::optional<domain::SeriesRow>& selected)
{
    selectedEntry_ = selected ? std::optional<qint64>(selected->entryId) : std::nullopt;
    card_->setVisible(selected.has_value());
    if (selected) {
        const auto title = selected->title();
        QString text = QStringLiteral("<b>%1</b>")
                           .arg(title ? QString::fromStdString(*title).toHtmlEscaped() : tr("Untitled volume"));
        if (selected->position)
            text += QStringLiteral(" · ") + QString::fromStdString(*selected->position).toHtmlEscaped();
        text += QStringLiteral(" · ") + (selected->owned() ? tr("on the shelf") : tr("not owned"));
        cardTitle_->setText(text);
        markOwned_->setVisible(!selected->owned());
    }

    const auto& series = detail.series;
    name_->setText(QString::fromStdString(series.name));
    QString byline = lengthText(series);
    if (detail.authors)
        byline = QString::fromStdString(*detail.authors) + QStringLiteral(" · ") + byline;
    byline_->setText(byline);

    progress_->setRange(0, std::max(series.known, 1));
    progress_->setValue(series.held);
    held_->setText(tr("%1 of %2").arg(series.held).arg(series.known));

    const int missing = series.known - series.held;
    QStringList legend;
    legend << tr("%1 read").arg(series.heldRead) << tr("%1 unread").arg(series.held - series.heldRead);
    if (missing > 0)
        legend << tr("%1 missing").arg(missing);
    legend_->setText(legend.join(QStringLiteral(" · ")));

    if (detail.missing.size() == 1) {
        missingHeading_->setText(tr("One volume short").toUpper());
        missing_->setText(tr("<b>%1</b><br>the only entry missing from this series")
                              .arg(volumeName(detail.missing.front())));
    } else if (!detail.missing.empty()) {
        missingHeading_->setText(tr("Missing · %1").arg(detail.missing.size()).toUpper());
        QStringList names;
        for (std::size_t i = 0; i < detail.missing.size() && i < shownMissing; ++i)
            names << volumeName(detail.missing[i]);
        QString text = names.join(QStringLiteral("<br>"));
        if (detail.missing.size() > shownMissing)
            text += QStringLiteral("<br>") + tr("and %1 more").arg(detail.missing.size() - shownMissing);
        missing_->setText(text);
    } else {
        missingHeading_->setText(tr("Missing").toUpper());
        if (series.status == "Complete to date")
            missing_->setText(tr("Nothing published is missing; the series is still being written."));
        else if (series.status == "Complete")
            missing_->setText(tr("Nothing. The series is complete."));
        else
            missing_->setText(tr("Nothing is recorded about what this series contains."));
    }

    oneShort_->setText(QString::number(detail.library.oneVolumeShort));
    complete_->setText(QString::number(detail.library.complete));
    withGaps_->setText(QString::number(detail.library.withGaps));
    notOwned_->setText(QString::number(detail.library.volumesNotOwned));
}

} // namespace pinax::ui

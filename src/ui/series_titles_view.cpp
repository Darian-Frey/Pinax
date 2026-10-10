#include "ui/series_titles_view.h"

#include "ui/style.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace pinax::ui {

namespace {

enum Column { TitleColumn, NumberColumn, GoesToColumn };

constexpr int goesToWidth = 104;

QString text(const std::string& value)
{
    return QString::fromStdString(value);
}

// Short, for a narrow panel: "No. 3", the position as printed ("Broadcast
// 6"), or, for the nth volume with no number, "Unnamed n".
QString volumeName(const domain::SeriesRow& row, int unnumbered)
{
    if (!row.position || row.position->empty())
        return QObject::tr("Unnamed %1").arg(unnumbered);
    const QString position = text(*row.position);
    bool numeric = false;
    position.toDouble(&numeric);
    return numeric ? QObject::tr("No. %1").arg(position) : position;
}

} // namespace

SeriesTitlesView::SeriesTitlesView(QWidget* parent)
    : QWidget(parent)
    , heading_(new QLabel(this))
    , status_(new QLabel(this))
    , list_(new QTreeWidget(this))
    , use_(new QPushButton(tr("Use ticked titles"), this))
    , cancel_(new QPushButton(tr("Cancel"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    layout->addWidget(makeSectionHeading(tr("Find titles"), this));

    heading_->setObjectName(QStringLiteral("titles.heading"));
    QFont font = heading_->font();
    font.setBold(true);
    heading_->setFont(font);
    heading_->setWordWrap(true);
    layout->addWidget(heading_);

    status_->setObjectName(QStringLiteral("titles.status"));
    status_->setWordWrap(true);
    setMuted(status_);
    layout->addWidget(status_);

    list_->setObjectName(QStringLiteral("titles.list"));
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->setSelectionMode(QAbstractItemView::NoSelection);
    list_->setHeaderLabels({tr("Title"), tr("No."), tr("Goes to")});
    list_->setTextElideMode(Qt::ElideRight);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(TitleColumn, QHeaderView::Stretch);
    list_->header()->setSectionResizeMode(NumberColumn, QHeaderView::ResizeToContents);
    list_->header()->setSectionResizeMode(GoesToColumn, QHeaderView::Fixed);
    list_->header()->resizeSection(GoesToColumn, goesToWidth);
    layout->addWidget(list_, 1);
    connect(list_, &QTreeWidget::itemChanged, this, &SeriesTitlesView::updateUse);

    auto* buttons = new QHBoxLayout;
    use_->setObjectName(QStringLiteral("titles.use"));
    use_->setDefault(true);
    cancel_->setObjectName(QStringLiteral("titles.cancel"));
    cancel_->setAutoDefault(false);
    buttons->addWidget(use_);
    buttons->addWidget(cancel_);
    buttons->addStretch();
    layout->addLayout(buttons);

    connect(use_, &QPushButton::clicked, this, &SeriesTitlesView::useRequested);
    connect(cancel_, &QPushButton::clicked, this, &SeriesTitlesView::cancelled);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &SeriesTitlesView::cancelled);
}

void SeriesTitlesView::showLooking(const QString& seriesName)
{
    proposals_.clear();
    targets_.clear();
    list_->clear();
    list_->hide();
    heading_->setText(seriesName);
    status_->setText(tr("Asking Wikidata for the volumes of this series, then Open Library if it has none…"));
    use_->hide();
    cancel_->setText(tr("Cancel"));
}

void SeriesTitlesView::showProposals(const QString& seriesName, const domain::SeriesFind& find,
    const std::vector<domain::TitleProposal>& proposals, const std::vector<domain::SeriesRow>& rows)
{
    proposals_ = proposals;
    targets_.clear();
    heading_->setText(seriesName);

    // Each choice, short, with what it is in full.
    QStringList targetNames {tr("New")};
    QStringList targetTips {tr("A new volume of the series")};
    int unnumbered = 0;
    for (const auto& row : rows) {
        if (domain::isUnidentified(row)) {
            targets_.push_back(row.entryId);
            const bool numbered = row.position && !row.position->empty();
            targetNames << volumeName(row, numbered ? 0 : ++unnumbered);
            targetTips << (row.entryTitle && !row.entryTitle->empty()
                    ? text(*row.entryTitle)
                    : tr("The volume at %1, without a title").arg(targetNames.back()));
        }
    }

    const QString from = text(find.provider) + (find.seriesName.empty() || find.seriesName == seriesName.toStdString()
                                                       ? QString()
                                                       : tr(" (as “%1”)").arg(text(find.seriesName)));
    if (proposals_.empty()) {
        status_->setText(find.volumes.empty()
                ? tr("Neither Wikidata nor Open Library lists the volumes of this series.")
                : tr("%1 lists %n volume(s), and the series already has every one of them.", nullptr,
                      static_cast<int>(find.volumes.size()))
                      .arg(from));
        list_->hide();
        use_->hide();
        cancel_->setText(tr("Close"));
        return;
    }
    const bool numbered = std::any_of(find.volumes.begin(), find.volumes.end(),
        [](const auto& volume) { return volume.ordinal.has_value(); });
    status_->setText(tr("From %1. Tick the titles that are right; nothing changes until you use them.")
                         .arg(from)
        + (numbered ? QString() : tr(" These are not numbered: choose which volume each goes to.")));

    const QSignalBlocker blocker(list_);
    list_->clear();
    for (std::size_t index = 0; index < proposals_.size(); ++index) {
        const auto& proposal = proposals_[index];
        auto* item = new QTreeWidgetItem(list_);
        item->setText(TitleColumn, text(proposal.volume.title));
        QString about = text(proposal.volume.title);
        if (proposal.volume.year)
            about += tr(", first published %1").arg(*proposal.volume.year);
        if (!proposal.volume.authors.empty())
            about += tr(", by %1").arg(text(proposal.volume.authors.front()));
        item->setToolTip(TitleColumn, about);
        item->setText(NumberColumn, proposal.volume.ordinal ? text(*proposal.volume.ordinal) : QString());
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(TitleColumn, proposal.chosen ? Qt::Checked : Qt::Unchecked);

        auto* goesTo = new QComboBox(list_);
        goesTo->setObjectName(QStringLiteral("titles.goesTo.%1").arg(index));
        goesTo->addItems(targetNames);
        for (int choice = 0; choice < targetTips.size(); ++choice)
            goesTo->setItemData(choice, targetTips[choice], Qt::ToolTipRole);
        goesTo->setFixedWidth(goesToWidth);
        goesTo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        goesTo->setToolTip(tr("Where this title goes: a volume without a title, or a new one"));
        int choice = 0;
        if (proposal.entryId) {
            const auto found = std::find(targets_.begin(), targets_.end(), *proposal.entryId);
            if (found != targets_.end())
                choice = static_cast<int>(found - targets_.begin()) + 1;
        }
        goesTo->setCurrentIndex(choice);
        list_->setItemWidget(item, GoesToColumn, goesTo);
        // Choosing where a title goes is choosing it.
        connect(goesTo, &QComboBox::activated, this, [item] { item->setCheckState(TitleColumn, Qt::Checked); });
    }
    list_->show();
    use_->show();
    cancel_->setText(tr("Cancel"));
    updateUse();
}

void SeriesTitlesView::showProblem(const QString& message)
{
    list_->hide();
    use_->hide();
    status_->setText(message);
    cancel_->setText(tr("Close"));
}

std::vector<domain::TitleProposal> SeriesTitlesView::accepted()
{
    std::vector<domain::TitleProposal> chosen;
    std::set<std::int64_t> used;
    for (int index = 0; index < list_->topLevelItemCount(); ++index) {
        QTreeWidgetItem* item = list_->topLevelItem(index);
        if (item->checkState(TitleColumn) != Qt::Checked)
            continue;
        domain::TitleProposal proposal = proposals_.at(static_cast<std::size_t>(index));
        const auto* goesTo = qobject_cast<QComboBox*>(list_->itemWidget(item, GoesToColumn));
        const int choice = goesTo ? goesTo->currentIndex() : 0;
        proposal.entryId = choice > 0 ? std::optional(targets_.at(static_cast<std::size_t>(choice - 1))) : std::nullopt;
        if (proposal.entryId && !used.insert(*proposal.entryId).second) {
            status_->setText(tr("Two ticked titles go to %1. Send one elsewhere, or untick it.")
                                 .arg(goesTo->currentText()));
            return {};
        }
        chosen.push_back(std::move(proposal));
    }
    return chosen;
}

void SeriesTitlesView::updateUse()
{
    bool any = false;
    for (int index = 0; index < list_->topLevelItemCount() && !any; ++index)
        any = list_->topLevelItem(index)->checkState(TitleColumn) == Qt::Checked;
    use_->setEnabled(any);
}

} // namespace pinax::ui

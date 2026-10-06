#include "ui/rail_view.h"

#include "ui/style.h"

#include <QHeaderView>
#include <QStandardItemModel>

namespace pinax::ui {

using domain::BookFilter;
using domain::ReadStatus;

namespace {

constexpr int kindRole = Qt::UserRole + 1;
constexpr int readStatusRole = Qt::UserRole + 2;
constexpr int seriesRole = Qt::UserRole + 3;

QStandardItem* makeHeading(const QString& text, const QWidget* style)
{
    auto* item = new QStandardItem(text.toUpper());
    // Not enabled, so arrow keys pass over headings to the entries.
    item->setFlags(Qt::NoItemFlags);
    QFont font = item->font();
    font.setPointSizeF(font.pointSizeF() * 0.8);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 1.2);
    font.setBold(true);
    item->setFont(font);
    item->setForeground(muted(style));
    return item;
}

QList<QStandardItem*> makeEntry(const QString& name, const QString& count, const BookFilter& filter,
    const QWidget* style)
{
    auto* label = new QStandardItem(name);
    label->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    label->setData(static_cast<int>(filter.kind), kindRole);
    label->setData(static_cast<int>(filter.readStatus), readStatusRole);
    label->setData(QVariant::fromValue<qint64>(filter.seriesId), seriesRole);
    label->setToolTip(name);

    auto* number = new QStandardItem(count);
    number->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    number->setForeground(muted(style));
    return {label, number};
}

BookFilter filterOf(const QStandardItem* item)
{
    BookFilter filter;
    filter.kind = static_cast<BookFilter::Kind>(item->data(kindRole).toInt());
    filter.readStatus = static_cast<ReadStatus>(item->data(readStatusRole).toInt());
    filter.seriesId = item->data(seriesRole).toLongLong();
    return filter;
}

// Only the fields that identify an entry: a read state means nothing to a
// series entry, and the other way round.
bool sameEntry(const BookFilter& a, const BookFilter& b)
{
    if (a.kind != b.kind)
        return false;
    switch (a.kind) {
    case BookFilter::Kind::ReadState: return a.readStatus == b.readStatus;
    case BookFilter::Kind::Series: return a.seriesId == b.seriesId;
    case BookFilter::Kind::All:
    case BookFilter::Kind::MissingVolumes:
    case BookFilter::Kind::OneVolumeShort: break;
    }
    return true;
}

QString seriesTip(const domain::SeriesStatus& series)
{
    const int missing = series.known - series.held;
    if (series.status == "Incomplete") {
        return missing == 1 ? RailView::tr("Incomplete — one volume missing")
                            : RailView::tr("Incomplete — %1 volumes missing").arg(missing);
    }
    if (series.status == "Complete to date")
        return RailView::tr("Complete to date — still being written");
    if (series.status == "Complete")
        return RailView::tr("Complete");
    return RailView::tr("Nothing recorded about what this series contains");
}

} // namespace

RailView::RailView(QWidget* parent)
    : QTreeView(parent)
    , model_(new QStandardItemModel(this))
{
    setModel(model_);
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setItemsExpandable(false);
    setIndentation(10);
    setUniformRowHeights(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setTextElideMode(Qt::ElideRight);
    setFrameShape(QFrame::NoFrame);

    connect(selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        [this](const QModelIndex& current) {
            if (rebuilding_ || !current.isValid())
                return;
            const QModelIndex first = current.siblingAtColumn(0);
            const QStandardItem* item = model_->itemFromIndex(first);
            if (!item || !(item->flags() & Qt::ItemIsSelectable))
                return;
            current_ = filterOf(item);
            emit filterChosen(current_, item->text());
        });
}

void RailView::setContents(const RailContents& contents)
{
    rebuilding_ = true;
    model_->clear();
    model_->setColumnCount(2);

    auto* library = makeHeading(tr("Library"), this);
    auto addLibrary = [&](const QString& name, std::int64_t count, BookFilter filter) {
        library->appendRow(makeEntry(name, QString::number(count), filter, this));
    };
    addLibrary(tr("All books"), contents.all, {});
    addLibrary(tr("Unread"), contents.unread, {BookFilter::Kind::ReadState, ReadStatus::Unread, 0});
    addLibrary(tr("Reading"), contents.reading, {BookFilter::Kind::ReadState, ReadStatus::Reading, 0});
    addLibrary(tr("Read"), contents.read, {BookFilter::Kind::ReadState, ReadStatus::Read, 0});
    model_->appendRow({library, new QStandardItem});

    auto* series = makeHeading(tr("Series · %1").arg(contents.series.size()), this);
    for (const auto& entry : contents.series) {
        BookFilter filter {BookFilter::Kind::Series, ReadStatus::Unread, entry.id};
        auto row = makeEntry(QString::fromStdString(entry.name),
            QStringLiteral("%1/%2").arg(entry.held).arg(entry.known), filter, this);
        row[0]->setToolTip(QString::fromStdString(entry.name) + QStringLiteral(" — ") + seriesTip(entry));
        row[1]->setToolTip(seriesTip(entry));
        series->appendRow(row);
    }
    model_->appendRow({series, new QStandardItem});

    auto* attention = makeHeading(tr("Needs attention"), this);
    attention->appendRow(makeEntry(tr("One volume short"), QString::number(contents.oneVolumeShort),
        {BookFilter::Kind::OneVolumeShort, ReadStatus::Unread, 0}, this));
    attention->appendRow(makeEntry(tr("Missing volumes"), QString::number(contents.missingVolumes),
        {BookFilter::Kind::MissingVolumes, ReadStatus::Unread, 0}, this));
    attention->child(0, 0)->setToolTip(tr("Series one volume from complete, and the volume each needs"));
    attention->child(1, 0)->setToolTip(tr("Every volume a series is known to contain and the shelf does not"));
    model_->appendRow({attention, new QStandardItem});

    for (int row = 0; row < model_->rowCount(); ++row)
        model_->item(row, 1)->setFlags(Qt::NoItemFlags);
    expandAll();
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    QStandardItem* keep = itemFor(current_);
    if (!keep) {
        current_ = {};
        keep = itemFor(current_);
    }
    selectItem(keep);
    rebuilding_ = false;
}

void RailView::chooseFilter(const BookFilter& filter)
{
    if (QStandardItem* item = itemFor(filter))
        selectItem(item); // emits through currentRowChanged
}

QStandardItem* RailView::itemFor(const BookFilter& filter) const
{
    for (int section = 0; section < model_->rowCount(); ++section) {
        QStandardItem* heading = model_->item(section, 0);
        for (int row = 0; row < heading->rowCount(); ++row) {
            QStandardItem* item = heading->child(row, 0);
            if (sameEntry(filterOf(item), filter))
                return item;
        }
    }
    return nullptr;
}

void RailView::selectItem(QStandardItem* item)
{
    if (!item)
        return;
    const QModelIndex index = item->index();
    selectionModel()->setCurrentIndex(index,
        QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollTo(index);
}

} // namespace pinax::ui

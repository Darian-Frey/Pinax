#include "ui/missing_page.h"

#include "domain/placeholder.h"
#include "ui/style.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QSet>
#include <QTableView>
#include <QVBoxLayout>

#include <utility>

namespace pinax::ui {

using domain::MissingRow;

MissingModel::MissingModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

void MissingModel::setRows(std::vector<MissingRow> rows)
{
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const MissingRow& MissingModel::row(int index) const
{
    return rows_.at(static_cast<std::size_t>(index));
}

int MissingModel::rowOfEntry(std::int64_t entryId) const
{
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].entryId == entryId)
            return static_cast<int>(i);
    }
    return -1;
}

int MissingModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int MissingModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant MissingModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const MissingRow& volume = row(index.row());
    const bool placeholder = domain::isPlaceholderTitle(volume.title);

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case SeriesColumn: return QString::fromStdString(volume.seriesName);
        case PositionColumn: return volume.position ? QString::fromStdString(*volume.position) : QString();
        case TitleColumn: return volume.title ? QString::fromStdString(*volume.title) : tr("Untitled volume");
        case NeedsColumn: return QString::number(volume.missingInSeries);
        }
        break;
    case Qt::FontRole:
        if (index.column() == TitleColumn) {
            QFont font;
            font.setItalic(true);
            return font;
        }
        break;
    case Qt::ForegroundRole:
        if (placeholder && index.column() == TitleColumn)
            return QColor(0x88, 0x88, 0x88);
        if (index.column() == NeedsColumn && volume.missingInSeries == 1)
            return accent();
        break;
    case Qt::ToolTipRole:
        if (index.column() == NeedsColumn) {
            return volume.missingInSeries == 1 ? tr("The only volume this series lacks")
                                               : tr("This series lacks %1 volumes").arg(volume.missingInSeries);
        }
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == PositionColumn || index.column() == NeedsColumn)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    }
    return {};
}

QVariant MissingModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case SeriesColumn: return tr("Series");
        case PositionColumn: return tr("No.");
        case TitleColumn: return tr("Title");
        case NeedsColumn: return tr("Needs");
        }
    }
    if (role == Qt::ToolTipRole && section == NeedsColumn)
        return tr("How many volumes the series lacks in all");
    if (role == Qt::TextAlignmentRole && (section == PositionColumn || section == NeedsColumn))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

MissingPage::MissingPage(QWidget* parent)
    : QWidget(parent)
    , heading_(new QLabel(this))
    , model_(new MissingModel(this))
    , table_(new QTableView(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(10, 6, 10, 6);
    heading_->setObjectName(QStringLiteral("missing.heading"));
    heading_->setTextFormat(Qt::RichText);
    bar->addWidget(heading_, 1);
    layout->addLayout(bar);
    layout->addWidget(table_, 1);

    table_->setModel(model_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setShowGrid(false);
    table_->setWordWrap(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table_->verticalHeader()->setDefaultSectionSize(34);
    QHeaderView* header = table_->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setSectionResizeMode(MissingModel::TitleColumn, QHeaderView::Stretch);
    header->resizeSection(MissingModel::SeriesColumn, 200);
    header->resizeSection(MissingModel::PositionColumn, 64);
    header->resizeSection(MissingModel::NeedsColumn, 56);

    auto report = [this] { emit selectionChangedTo(selectedVolume()); };
    connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged, this, report);
    connect(model_, &QAbstractItemModel::modelReset, this, report);
    connect(table_, &QTableView::activated, this, [this](const QModelIndex& index) {
        emit openSeriesRequested(model_->row(index.row()).seriesId);
    });
}

void MissingPage::showRows(std::vector<MissingRow> rows, bool oneVolumeShort)
{
    const auto keep = selectedVolume();
    oneVolumeShort_ = oneVolumeShort;

    QSet<qint64> series;
    for (const auto& row : rows)
        series.insert(row.seriesId);
    const int volumes = static_cast<int>(rows.size());
    heading_->setText(oneVolumeShort
            ? tr("<b>One volume short</b>&nbsp;&nbsp;%1 series, each needing one").arg(series.size())
            : tr("<b>Missing volumes</b>&nbsp;&nbsp;%1 across %2 series, fewest needed first")
                  .arg(volumes)
                  .arg(series.size()));

    const QSignalBlocker block(table_->selectionModel());
    model_->setRows(std::move(rows));
    if (keep) {
        const int row = model_->rowOfEntry(keep->entryId);
        if (row >= 0)
            table_->selectRow(row);
    }
}

std::optional<MissingRow> MissingPage::selectedVolume() const
{
    const QModelIndexList rows = table_->selectionModel()->selectedRows();
    if (rows.size() != 1)
        return std::nullopt;
    return model_->row(rows.front().row());
}

} // namespace pinax::ui

#pragma once

#include "domain/series_row.h"
#include "domain/series_titles.h"

#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace pinax::ui {

// Find titles, in the panel (F-030): the lookup under way, then what was
// found, each title against the volume it would name, or as a new volume.
// The owner ticks the right ones, and may send a title to another
// unidentified volume. Writes nothing itself (AV-010).
class SeriesTitlesView : public QWidget {
    Q_OBJECT

public:
    explicit SeriesTitlesView(QWidget* parent = nullptr);

    void showLooking(const QString& seriesName);
    // `rows` is the series as it stands, for naming its unidentified
    // volumes in the "Goes to" choice.
    void showProposals(const QString& seriesName, const domain::SeriesFind& find,
        const std::vector<domain::TitleProposal>& proposals, const std::vector<domain::SeriesRow>& rows);
    void showProblem(const QString& message);

    // The ticked proposals, each going where the owner chose; empty when two
    // go to the same volume, with the reason shown.
    std::vector<domain::TitleProposal> accepted();

signals:
    // Use ticked titles pressed; the caller reads accepted().
    void useRequested();
    void cancelled();

private:
    void updateUse();

    QLabel* heading_;
    QLabel* status_;
    QTreeWidget* list_;
    QPushButton* use_;
    QPushButton* cancel_;
    std::vector<domain::TitleProposal> proposals_;
    std::vector<std::int64_t> targets_; // the unidentified entries, in the choices' order after "New volume"
};

} // namespace pinax::ui

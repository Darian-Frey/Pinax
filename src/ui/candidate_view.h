#pragma once

#include "domain/candidate.h"

#include <QWidget>

#include <vector>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace pinax::ui {

// "Fetch metadata" in the panel (F-012, D-011): the lookup under way, then
// what the providers offered, for the owner to choose from. Nothing is
// written until a candidate is chosen — not even for an ISBN match, since an
// ISBN can name another book (AV-010).
class CandidateView : public QWidget {
    Q_OBJECT

public:
    explicit CandidateView(QWidget* parent = nullptr);

    // The lookup has started; `how` says what is being asked of whom.
    void begin(const QString& title, const QString& how);
    // Reviewing a batch run's finds (D-023): `place` says where in the queue
    // ("3 of 41"), and Not my book and Skip are offered beside Use this.
    void beginReview(const QString& title, const QString& place);
    void setProgress(const QString& text);
    // `byIsbn`: the candidates answer the book's ISBN, and so describe its
    // edition; otherwise they were found by title and author.
    void offer(const std::vector<domain::Candidate>& candidates, bool byIsbn);
    // The cover of the candidate at `index`, when it has arrived: shown
    // beside it, so editions can be told apart before one is chosen; and
    // at twice the size while the pointer rests on it.
    void setCover(int index, const QPixmap& cover);

    // The enlarged cover on show, or nullptr when none is.
    const QLabel* coverPreview() const;

    // Nothing to choose from, and why.
    void showProblem(const QString& message);

    void focusList();

signals:
    // `myEdition`: the owner ticked "This is my edition" (D-029); always
    // true for an ISBN's answer, which is the owner's edition already.
    void chosen(int index, bool myEdition);
    void cancelled();
    // Reviewing: none of the candidates is the book; or not decided now.
    void rejected();
    void skipped();

protected:
    // The list's pointer movements: over a thumbnail, its cover enlarged.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void previewCover(int row, const QPoint& globalPosition);
    void hidePreview();

    void choose();

    QLabel* heading_;
    QLabel* status_;
    QListWidget* list_;
    QLabel* preview_;
    std::vector<QPixmap> covers_; // as downloaded, by row; null until it arrives
    int previewRow_ = -1;
    QPushButton* use_;
    QPushButton* cancel_;
    QCheckBox* myEdition_;
    bool byIsbn_ = false;
    QPushButton* reject_;
    QPushButton* skip_;
    bool reviewing_ = false;
    QString place_;
};

} // namespace pinax::ui

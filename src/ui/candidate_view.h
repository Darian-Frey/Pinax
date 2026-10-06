#pragma once

#include "domain/candidate.h"

#include <QWidget>

#include <vector>

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
    // Nothing to choose from, and why.
    void showProblem(const QString& message);

    void focusList();

signals:
    void chosen(int index);
    void cancelled();
    // Reviewing: none of the candidates is the book; or not decided now.
    void rejected();
    void skipped();

private:
    void choose();

    QLabel* heading_;
    QLabel* status_;
    QListWidget* list_;
    QPushButton* use_;
    QPushButton* cancel_;
    QPushButton* reject_;
    QPushButton* skip_;
    bool reviewing_ = false;
    QString place_;
};

} // namespace pinax::ui

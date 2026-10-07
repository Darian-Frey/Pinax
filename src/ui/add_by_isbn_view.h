#pragma once

#include "domain/candidate.h"
#include "domain/credit_text.h"
#include "domain/enums.h"
#include "domain/series_proposal.h"

#include <QWidget>

#include <optional>
#include <string>
#include <vector>

class QButtonGroup;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QStackedWidget;
class QVBoxLayout;

namespace pinax::ui {

// Add a book by ISBN in the panel (F-024, D-012): the ISBN, the lookup, then
// a card to check — cover, title, authors, edition facts, where it goes in a
// series and whether it is read — before anything is written. Declining
// falls back to a search by title and author, and that to the ordinary form.
// Issues no lookups or writes itself: it asks, and is told what was found.
class AddByIsbnView : public QWidget {
    Q_OBJECT

public:
    // A book already held, without an ISBN, that looks like this one.
    struct HeldBook {
        qint64 id = 0;
        QString description; // "Titan — Stephen Baxter · NASA Trilogy · 2"
    };

    struct Choice {
        int candidate = 0;
        // The owner's existing copy, to be given this ISBN instead of a new
        // book being added; the fields below are then unused.
        std::optional<qint64> existingBook;
        std::string title;
        std::optional<std::string> subtitle;
        std::vector<domain::NamedCredit> credits;
        domain::ReadStatus readStatus = domain::ReadStatus::Unread;
        std::optional<domain::SeriesProposal> series; // with position as edited
    };

    explicit AddByIsbnView(QWidget* parent = nullptr);

    // An empty ISBN field.
    void start();
    void showWaiting(const QString& text);
    void showDuplicate(qint64 bookId, const QString& title);
    // `byIsbn`: the candidates answer the ISBN, and the first is shown; else
    // they came from a search and none is chosen until the owner picks one
    // (AV-010).
    void showCandidates(const std::vector<domain::Candidate>& candidates, bool byIsbn);
    // For the candidate on show: authors in the catalogue's spelling, and
    // where it could go in a series.
    void setCardDetails(const std::vector<domain::NamedCredit>& credits,
        const std::vector<domain::SeriesProposal>& proposals, const std::vector<HeldBook>& held = {});
    void setCover(const QPixmap& cover);
    // Nothing to show, or the owner declined: search by title and author,
    // prefilled; or enter the book by hand.
    void showSearch(const QString& message, const QString& title, const QString& author);
    void showError(const QString& message);

    // The ISBN as typed, validated: ISBN-13, and the ISBN-10 if that is what
    // was typed.
    std::string isbn13() const { return isbn13_; }
    std::optional<std::string> isbn10() const { return isbn10_; }
    int shownCandidate() const;
    // What the search stage holds, for entering the book by hand.
    QString searchedTitle() const;
    QString searchedAuthor() const;

    void focusIsbn();

signals:
    void lookupRequested(const QString& isbn13);
    void candidateShown(int index);
    void addRequested(const pinax::ui::AddByIsbnView::Choice& choice);
    void notThisBook(int index);
    void searchRequested(const QString& title, const QString& author);
    void manualRequested();
    void showBookRequested(qint64 bookId);
    void cancelled();

private:
    QWidget* makeEntry();
    QWidget* makeWaiting();
    QWidget* makeDuplicate();
    QWidget* makeCard();
    QWidget* makeSearch();
    void lookUp();
    void showCandidate(int index);
    void add();
    void updateAddButton();
    // Whether the owner has said this is a copy already held.
    bool givingIsbn() const;

    QStackedWidget* stages_;
    QLabel* heading_;

    QLineEdit* isbn_;
    QLabel* isbnError_;

    QLabel* waiting_;

    QLabel* duplicate_;
    qint64 duplicateId_ = 0;

    QComboBox* candidates_;
    QLabel* cover_;
    QLineEdit* title_;
    QLineEdit* authors_;
    QLabel* facts_;
    QWidget* heldBox_;
    QVBoxLayout* heldOptions_;
    QButtonGroup* heldGroup_;
    std::vector<HeldBook> held_;
    QWidget* seriesBox_;
    QLabel* seriesHeading_;
    QLabel* shelfHeading_;
    QVBoxLayout* seriesOptions_;
    QButtonGroup* seriesGroup_;
    QWidget* placement_;
    QLineEdit* position_;
    QDoubleSpinBox* sortPosition_;
    QRadioButton* unread_;
    QRadioButton* read_;
    QLabel* cardError_;
    QPushButton* add_;

    QLabel* searchMessage_;
    QLineEdit* searchTitle_;
    QLineEdit* searchAuthor_;

    std::vector<domain::Candidate> shown_;
    std::vector<domain::SeriesProposal> proposals_;
    bool byIsbn_ = true;
    std::string isbn13_;
    std::optional<std::string> isbn10_;
};

} // namespace pinax::ui

#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace pinax::ui {

// An outcome to read in the panel — an import's report line by line, a
// restore, a catalogue that would not open (F-026 to F-028) — with Close.
// Not a form: the list stays free while it is shown.
class ReportView : public QWidget {
    Q_OBJECT

public:
    explicit ReportView(QWidget* parent = nullptr);

    // `problem` colours the heading as a failure.
    void present(const QString& heading, const QString& body, bool problem);

signals:
    void closed();

private:
    QLabel* heading_;
    QLabel* body_;
    QPushButton* close_;
};

} // namespace pinax::ui

#pragma once

#include <QString>
#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace pinax::ui {

// Export the catalogue, in the panel (F-021 to F-023, D-011): a format, a
// path typed with folders and files offered, then the outcome. Writes
// nothing itself: it asks.
class ExportView : public QWidget {
    Q_OBJECT

public:
    struct Format {
        QString name;        // "SQL dump"
        QString extension;   // "sql"
        QString description; // what the file is for
    };

    explicit ExportView(QWidget* parent = nullptr);

    void setFormats(std::vector<Format> formats);
    // Ready to export, with this folder and file stem suggested.
    void start(const QString& folder, const QString& stem);
    void showDone(const QString& message);
    void showProblem(const QString& message);

    QString path() const;
    // A path chosen elsewhere (the save dialogue, D-027).
    void setPath(const QString& path);
    int format() const;
    void focusPath();

signals:
    void exportRequested(int format, const QString& path);
    void closed();
    // Choose… pressed: the caller offers the save dialogue.
    void chooseRequested();

private:
    void formatChosen();
    void updateWarning();

    std::vector<Format> formats_;
    QComboBox* format_;
    QLabel* description_;
    QLineEdit* path_;
    QPushButton* choose_;
    QLabel* warning_;
    QLabel* outcome_;
    QPushButton* export_;
    QPushButton* close_;
};

} // namespace pinax::ui

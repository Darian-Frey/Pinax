#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

namespace pinax::ui {

// Back up the catalogue, in the panel (F-020, D-011): where to, then the
// outcome. The path is typed — with folders and files offered as it is —
// rather than chosen in a dialogue. Writes nothing itself: it asks.
class BackupView : public QWidget {
    Q_OBJECT

public:
    explicit BackupView(QWidget* parent = nullptr);

    // Ready to back up to this path, suggested.
    void start(const QString& suggestedPath);
    void showDone(const QString& message);
    void showProblem(const QString& message);

    QString path() const;
    void focusPath();

signals:
    void backupRequested(const QString& path);
    void closed();

private:
    void updateWarning();

    QLineEdit* path_;
    QLabel* warning_;
    QLabel* outcome_;
    QPushButton* backUp_;
    QPushButton* close_;
};

} // namespace pinax::ui

#include "ui/report_view.h"

#include "ui/style.h"

#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

namespace pinax::ui {

ReportView::ReportView(QWidget* parent)
    : QWidget(parent)
    , heading_(new QLabel(this))
    , body_(new QLabel(this))
    , close_(new QPushButton(tr("Close"), this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(8);
    heading_->setObjectName(QStringLiteral("report.heading"));
    heading_->setWordWrap(true);
    QFont font = heading_->font();
    font.setBold(true);
    heading_->setFont(font);
    body_->setObjectName(QStringLiteral("report.body"));
    body_->setWordWrap(true);
    body_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body_->setTextFormat(Qt::PlainText);
    close_->setObjectName(QStringLiteral("report.close"));
    layout->addWidget(heading_);
    layout->addWidget(body_);
    layout->addWidget(close_, 0, Qt::AlignLeft);
    layout->addStretch();
    connect(close_, &QPushButton::clicked, this, &ReportView::closed);
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &ReportView::closed);
}

void ReportView::present(const QString& heading, const QString& body, bool problem)
{
    heading_->setText(heading);
    heading_->setStyleSheet(problem ? QStringLiteral("color: #d9534f;") : QString());
    body_->setText(body);
}

} // namespace pinax::ui

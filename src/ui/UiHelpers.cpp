#include "UiHelpers.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableView>

namespace {

// Text of the cells a few pixels away from the cell borders; the background and the
// selection still fill the whole cell, widgets placed in cells are not affected.
class PaddedDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int kPad = 4;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem bg(option);
        initStyleOption(&bg, index);
        const QWidget* w = option.widget;
        QStyle* style = w ? w->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &bg, painter, w);
        QStyleOptionViewItem inner(option);
        inner.rect.adjust(kPad, 0, -kPad, 0);
        QStyledItemDelegate::paint(painter, inner, index);
    }
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        return QStyledItemDelegate::sizeHint(option, index) + QSize(2 * kPad, 0);
    }
};

} // namespace

namespace UiHelpers {

QIcon icon(const QString& name)
{
    return QIcon(QStringLiteral(":/icons/%1.svg").arg(name));
}

QPushButton* button(const QString& iconName, const QString& text, QWidget* parent)
{
    auto* b = new QPushButton(icon(iconName), text, parent);
    b->setIconSize(QSize(22, 22));
    b->setMinimumHeight(34);
    b->setProperty("skiproButton", true);
    return b;
}

QLabel* titleBar(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName(QStringLiteral("titleBar"));
    l->setMinimumHeight(22);
    l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return l;
}

QLabel* infoBox(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setObjectName(QStringLiteral("infoBox"));
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumHeight(32);
    return l;
}

QLabel* counterBox(QWidget* parent)
{
    auto* l = new QLabel(QStringLiteral("0"), parent);
    l->setObjectName(QStringLiteral("counterBox"));
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumSize(60, 26);
    return l;
}

QHBoxLayout* okCancelRow(QDialog* dlg, QPushButton** load, QPushButton** save)
{
    auto* row = new QHBoxLayout;
    if (load) {
        *load = button(QStringLiteral("folder"), QCoreApplication::translate("UiHelpers", "Load"), dlg);
        (*load)->setMinimumWidth(120);
        row->addWidget(*load);
    }
    if (save) {
        *save = button(QStringLiteral("save"), QCoreApplication::translate("UiHelpers", "Save"), dlg);
        (*save)->setMinimumWidth(120);
        row->addWidget(*save);
    }
    row->addStretch();
    auto* ok = button(QStringLiteral("ok"), QCoreApplication::translate("UiHelpers", "Ok"), dlg);
    auto* cancel = button(QStringLiteral("cancel"), QCoreApplication::translate("UiHelpers", "Cancel"), dlg);
    ok->setMinimumWidth(120);
    cancel->setMinimumWidth(120);
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, dlg, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, dlg, &QDialog::reject);
    row->addWidget(ok);
    row->addWidget(cancel);
    return row;
}

void styleGrid(QTableView* view, bool monospace)
{
    view->setAlternatingRowColors(true);
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->verticalHeader()->setVisible(false);
    view->verticalHeader()->setDefaultSectionSize(monospace ? 22 : 21);
    view->horizontalHeader()->setHighlightSections(false);
    view->setWordWrap(false);
    if (monospace) {
        QFont f(QStringLiteral("Courier New"));
        f.setPointSize(10);
        view->setFont(f);
    } else {
        // some space between the text and the cell borders (the timing grids stay compact)
        view->setItemDelegate(new PaddedDelegate(view));
    }
}

QString oneLine(const QString& text)
{
    QStringList lines;
    for (const QString& l : text.split(QLatin1Char('\n')))
        if (!l.trimmed().isEmpty())
            lines << l.trimmed();
    return lines.join(QStringLiteral(" / "));
}

QString statusText(ResultStatus s)
{
    switch (s) {
    case ResultStatus::Classified: return QCoreApplication::translate("UiHelpers", "Time");
    case ResultStatus::DNS: return QCoreApplication::translate("UiHelpers", "Did Not Start");
    case ResultStatus::NPS: return QCoreApplication::translate("UiHelpers", "Not Permitted to Start");
    case ResultStatus::DNF: return QCoreApplication::translate("UiHelpers", "Did Not Finish");
    case ResultStatus::DSQ: return QCoreApplication::translate("UiHelpers", "Disqualified");
    case ResultStatus::NotProcessed: return QCoreApplication::translate("UiHelpers", "Not Processed");
    }
    return QString();
}

QString pointName(int point)
{
    if (point == Point::Start)
        return QCoreApplication::translate("UiHelpers", "Start");
    if (point == Point::Finish)
        return QCoreApplication::translate("UiHelpers", "Finish");
    return QCoreApplication::translate("UiHelpers", "Inter %1").arg(point);
}

QString appStyleSheet()
{
    return QStringLiteral(
        "QLabel#titleBar { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #dce8f6, stop:1 #a9c4e6);"
        "  border: 1px solid #3c5f8c; border-radius: 2px; padding: 1px 6px; font-weight: bold; font-size: 10pt; color: #10233d; }"
        "QLabel#infoBox { background: #ffffe1; border: 1px solid #b9b98a; border-radius: 2px; font-weight: bold; padding: 4px; }"
        "QLabel#counterBox { background: #ffffe1; border: 1px solid #b9b98a; border-radius: 2px; font-weight: bold; font-size: 10pt; }"
        "QWidget#navPanel { background: #bcd4ee; }"
        "QLabel#navHeader { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #e3edf8, stop:1 #b2cae8);"
        "  border: 1px solid #2f4f7a; border-radius: 2px; font-weight: bold; font-size: 10pt; padding: 1px 4px; color: #0b1d33; }"
        "QToolButton#navButton { border: none; padding: 3px 4px; text-align: left; color: #10233d; }"
        "QToolButton#navButton:hover { color: #0040c0; text-decoration: underline; }"
        "QToolButton#navButton:disabled { color: #7a8ca3; }"
        "QWidget#headerBanner { background: white; border-bottom: 2px solid #3c5f8c; }"
        "QTableView { gridline-color: #c8d3e0; selection-background-color: #3d6fb4; alternate-background-color: #eef4fb; }"
        "QTableView#paperGrid { background: #ffffe1; alternate-background-color: #fff8c8; }"
        "QHeaderView::section { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #f4f7fb, stop:1 #d5e0ee);"
        "  border: 1px solid #a7b8cf; padding: 2px 4px; }");
}

} // namespace UiHelpers

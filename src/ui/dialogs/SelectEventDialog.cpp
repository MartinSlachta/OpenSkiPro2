#include "SelectEventDialog.h"
#include "db/EventDb.h"
#include "ui/MainWindow.h"
#include "ui/UiHelpers.h"

#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QTableWidget>
#include <QVBoxLayout>

static constexpr int kSlots = 999;

SelectEventDialog::SelectEventDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    setWindowTitle(tr("Select Event"));
    auto* lay = new QVBoxLayout(this);
    m_table = new QTableWidget(kSlots, 4, this);
    m_table->setHorizontalHeaderLabels({ tr("Title"), QString(), tr("Date"), tr("Runs") });
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    UiHelpers::styleGrid(m_table);
    m_table->verticalHeader()->setVisible(true);
    m_table->verticalHeader()->setDefaultSectionSize(30);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->setColumnWidth(1, 30);
    m_table->setColumnWidth(2, 110);
    m_table->setColumnWidth(3, 60);
    int firstExisting = -1;
    int firstFree = -1;
    for (int i = 0; i < kSlots; ++i) {
        const QString path = MainWindow::eventPath(i + 1);
        QString t1, t2;
        QDate date;
        int runs = 0;
        const bool exists = QFileInfo::exists(path) && EventDb::peek(path, &t1, &t2, &date, &runs);
        t1 = UiHelpers::oneLine(t1);
        t2 = UiHelpers::oneLine(t2);
        auto* title = new QTableWidgetItem(exists ? (t1 + (t2.isEmpty() ? QString() : QStringLiteral("\n") + t2)) : tr("N.C."));
        auto* ball = new QTableWidgetItem;
        ball->setIcon(UiHelpers::icon(exists ? QStringLiteral("ball_green") : QStringLiteral("ball_red")));
        auto* d = new QTableWidgetItem(date.isValid() ? QLocale::system().toString(date, QLocale::ShortFormat) : QString());
        m_table->setItem(i, 0, title);
        m_table->setItem(i, 1, ball);
        m_table->setItem(i, 2, d);
        auto* r = new QTableWidgetItem(exists && runs > 0 ? QString::number(runs) : QString());
        r->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 3, r);
        if (exists) {
            m_table->setRowHeight(i, 40);
            if (firstExisting < 0)
                firstExisting = i;
        } else if (firstFree < 0) {
            firstFree = i;
        }
        title->setData(Qt::UserRole, exists);
    }
    const int sel = (mode == Mode::FreeSlot) ? firstFree : (firstExisting >= 0 ? firstExisting : firstFree);
    if (sel >= 0) {
        m_table->selectRow(sel);
        m_table->scrollToItem(m_table->item(sel, 0));
    }
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &SelectEventDialog::accept);
    lay->addWidget(m_table, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(680, 820);
}

void SelectEventDialog::accept()
{
    const auto rows = m_table->selectionModel()->selectedRows();
    if (rows.isEmpty())
        return;
    const int row = rows.first().row();
    const bool exists = m_table->item(row, 0)->data(Qt::UserRole).toBool();
    if (m_mode == Mode::FreeSlot && exists) {
        QMessageBox::warning(this, windowTitle(), tr("This location is already used"));
        return;
    }
    if (m_mode == Mode::Existing && !exists) {
        QMessageBox::warning(this, windowTitle(), tr("No event at this location"));
        return;
    }
    m_slot = row + 1;
    QDialog::accept();
}

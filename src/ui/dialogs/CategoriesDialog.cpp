#include "CategoriesDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "SelectEventDialog.h"
#include "ui/MainWindow.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

CategoriesDialog::CategoriesDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    setWindowTitle(tr("Categories"));
    auto* lay = new QVBoxLayout(this);
    auto* row = new QHBoxLayout;
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({ tr("Category"), tr("Gender"), tr("Year from"), tr("Year to") });
    UiHelpers::styleGrid(m_table);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    row->addWidget(m_table, 1);
    auto* side = new QVBoxLayout;
    auto* add = new QToolButton(this);
    add->setIcon(UiHelpers::icon(QStringLiteral("add")));
    add->setIconSize(QSize(22, 22));
    auto* del = new QToolButton(this);
    del->setIcon(UiHelpers::icon(QStringLiteral("remove")));
    del->setIconSize(QSize(22, 22));
    side->addWidget(add);
    side->addWidget(del);
    side->addStretch();
    row->addLayout(side);
    lay->addLayout(row, 1);

    auto* actions = new QHBoxLayout;
    // like the report layouts: from a file, a SkiPro file or another event; saved to a file
    auto* load = UiHelpers::button(QStringLiteral("folder"), tr("Load"), this);
    auto* loadMenu = new QMenu(load);
    loadMenu->addAction(tr("Load from a File..."), this, &CategoriesDialog::loadFromFile);
    loadMenu->addAction(tr("Load a SkiPro category file (*.cat)..."), this, &CategoriesDialog::loadSkiPro);
    loadMenu->addAction(tr("Load from an Event..."), this, &CategoriesDialog::loadFromEvent);
    load->setMenu(loadMenu);
    auto* save = UiHelpers::button(QStringLiteral("save"), tr("Save to a File..."), this);
    auto* assignBtn = UiHelpers::button(QStringLiteral("competitor"), tr("Assign the categories to the competitors"), this);
    load->setMinimumWidth(110);
    actions->addWidget(load);
    actions->addWidget(save);
    actions->addStretch();
    actions->addWidget(assignBtn);
    actions->addStretch();
    lay->addLayout(actions);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(add, &QToolButton::clicked, this, [this] { addRow(CategoryDef()); });
    connect(del, &QToolButton::clicked, this, [this] {
        if (m_table->currentRow() >= 0)
            m_table->removeRow(m_table->currentRow());
    });
    connect(save, &QPushButton::clicked, this, &CategoriesDialog::saveToFile);
    connect(assignBtn, &QPushButton::clicked, this, &CategoriesDialog::assign);
    setGrid(ev->categories());
    resize(600, 500);
}

void CategoriesDialog::addRow(const CategoryDef& c)
{
    const int r = m_table->rowCount();
    m_table->insertRow(r);
    m_table->setItem(r, 0, new QTableWidgetItem(c.name));
    auto* g = new QComboBox(m_table);
    g->addItem(tr("Both"), int(Gender::Unknown));
    g->addItem(tr("Male"), int(Gender::Male));
    g->addItem(tr("Female"), int(Gender::Female));
    g->setCurrentIndex(qMax(0, g->findData(int(c.gender))));
    m_table->setCellWidget(r, 1, g);
    for (int col = 2; col <= 3; ++col) {
        auto* s = new QSpinBox(m_table);
        s->setRange(0, 2200);
        s->setSpecialValueText(QStringLiteral("-"));
        s->setValue(col == 2 ? c.yearMin : c.yearMax);
        m_table->setCellWidget(r, col, s);
    }
}

void CategoriesDialog::setGrid(const CategoryGrid& g)
{
    m_table->setRowCount(0);
    for (const auto& c : g.categories)
        addRow(c);
}

CategoryGrid CategoriesDialog::grid() const
{
    CategoryGrid g;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        CategoryDef c;
        c.name = m_table->item(r, 0) ? m_table->item(r, 0)->text().trimmed() : QString();
        if (c.name.isEmpty())
            continue;
        c.gender = Gender(static_cast<QComboBox*>(m_table->cellWidget(r, 1))->currentData().toInt());
        c.yearMin = static_cast<QSpinBox*>(m_table->cellWidget(r, 2))->value();
        c.yearMax = static_cast<QSpinBox*>(m_table->cellWidget(r, 3))->value();
        g.categories << c;
    }
    return g;
}

void CategoriesDialog::loadSkiPro()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Categories"), AppSettings::value(QStringLiteral("folders/skipro")).toString(),
                                                   tr("SkiPro categories (*.cat)"));
    if (f.isEmpty())
        return;
    QString err;
    const CategoryGrid g = CategoryGrid::fromSkiProCatFile(f, &err);
    if (g.categories.isEmpty()) {
        QMessageBox::warning(this, tr("Categories"), err);
        return;
    }
    setGrid(g);
}

// File of categories (*.ospcat): {"categories": [{"name", "gender", "yearMin", "yearMax"}, ...]}
void CategoriesDialog::saveToFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/categories"), AppSettings::exportDir()).toString();
    QDir().mkpath(dir);
    QString file = QFileDialog::getSaveFileName(this, tr("Save to a File"), QDir(dir).absoluteFilePath(QStringLiteral("categories.ospcat")),
                                                tr("OpenSkiPro2 categories (*.ospcat)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".ospcat"), Qt::CaseInsensitive))
        file += QStringLiteral(".ospcat");
    AppSettings::setValue(QStringLiteral("folders/categories"), QFileInfo(file).absolutePath());
    QJsonArray arr;
    for (const CategoryDef& c : grid().categories) {
        QJsonObject o;
        o[QStringLiteral("name")] = c.name;
        o[QStringLiteral("gender")] = c.gender == Gender::Male ? QStringLiteral("M") : c.gender == Gender::Female ? QStringLiteral("F")
                                                                                                                : QString();
        o[QStringLiteral("yearMin")] = c.yearMin;
        o[QStringLiteral("yearMax")] = c.yearMax;
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("categories")] = arr;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(root).toJson()) < 0)
        QMessageBox::warning(this, tr("Categories"), tr("Cannot write %1: %2").arg(file, f.errorString()));
}

void CategoriesDialog::loadFromFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/categories"), AppSettings::exportDir()).toString();
    const QString file = QFileDialog::getOpenFileName(this, tr("Load from a File"), dir, tr("OpenSkiPro2 categories (*.ospcat)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("folders/categories"), QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Categories"), tr("Cannot read %1: %2").arg(file, f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.value(QStringLiteral("categories")).isArray()) {
        QMessageBox::warning(this, tr("Categories"), tr("%1 is not a file of categories.").arg(file));
        return;
    }
    CategoryGrid g;
    for (const auto& v : root.value(QStringLiteral("categories")).toArray()) {
        const QJsonObject o = v.toObject();
        CategoryDef c;
        c.name = o.value(QStringLiteral("name")).toString().trimmed();
        c.gender = parseGender(o.value(QStringLiteral("gender")).toString());
        c.yearMin = o.value(QStringLiteral("yearMin")).toInt();
        c.yearMax = o.value(QStringLiteral("yearMax")).toInt();
        if (!c.name.isEmpty())
            g.categories << c;
    }
    setGrid(g);
}

void CategoriesDialog::loadFromEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("Load from an Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    CategoryGrid g;
    {
        Event other;
        QString err;
        if (!other.open(MainWindow::eventPath(dlg.selectedSlot()), &err)) {
            QMessageBox::warning(this, tr("Categories"), err);
            return;
        }
        g = other.categories();
        other.close();
    }
    if (g.categories.isEmpty()) {
        QMessageBox::information(this, tr("Categories"), tr("The selected event has no categories."));
        return;
    }
    setGrid(g);
}

void CategoriesDialog::assign()
{
    const CategoryGrid g = grid();
    int n = 0;
    m_ev->beginBatch();
    QVector<Competitor> list = m_ev->competitors();
    for (auto& c : list) {
        const QString cat = g.categoryFor(c.gender, c.year);
        if (cat != c.category) {
            c.category = cat;
            m_ev->updateCompetitor(c);
            ++n;
        }
    }
    m_ev->commitBatch();
    QMessageBox::information(this, tr("Categories"), tr("Category changed for %n competitor(s).", nullptr, n));
}

void CategoriesDialog::accept()
{
    m_ev->setCategories(grid());
    QDialog::accept();
}

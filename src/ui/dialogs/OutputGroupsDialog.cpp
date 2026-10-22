#include "OutputGroupsDialog.h"
#include "SelectEventDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "ui/MainWindow.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

namespace {

QString fieldLabel(const Event* ev, int id)
{
    const QString custom = ev->params().fields.labels.value(id);
    if (!custom.isEmpty())
        return custom;
    const CompetitorFieldDef* def = competitorFieldDef(id);
    return def ? QCoreApplication::translate("Competitor", def->label) : QString::number(id);
}

QString describe(const Event* ev, const OutputGroup& g)
{
    QStringList parts;
    for (const GroupCriterion& c : g.criteria) {
        QString value = c.value;
        if (c.op == GroupCriterion::Between)
            value += QStringLiteral(" - ") + c.value2;
        parts << QStringLiteral("%1 %2 %3").arg(fieldLabel(ev, c.field), groupOperatorText(c.op), value);
    }
    return parts.join(g.anyMode ? OutputGroupsDialog::tr(" or ") : OutputGroupsDialog::tr(" and "));
}

constexpr int kCriteriaRows = 6;

} // namespace

OutputGroupsDialog::OutputGroupsDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_groups(ev->params().outputGroups)
{
    setWindowTitle(tr("Output groups"));
    auto* lay = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("The reports printed \"by output groups\" list the competitors of each group under its name, "
                               "the rankings are computed inside each group. A competitor can be in several groups."),
                            this);
    hint->setWordWrap(true);
    lay->addWidget(hint);

    auto* row = new QHBoxLayout;
    auto* tools = new QVBoxLayout;
    auto tool = [this](const QString& icon, const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(icon));
        b->setIconSize(QSize(22, 22));
        b->setToolTip(tip);
        return b;
    };
    auto* add = tool(QStringLiteral("add"), tr("New group (F1)"));
    auto* edit = tool(QStringLiteral("modify"), tr("Modify the group (F2)"));
    auto* del = tool(QStringLiteral("remove"), tr("Delete the group"));
    auto* up = tool(QStringLiteral("up"), tr("Move up"));
    auto* down = tool(QStringLiteral("down"), tr("Move down"));
    for (auto* b : { add, edit, del })
        tools->addWidget(b);
    tools->addSpacing(8);
    tools->addWidget(up);
    tools->addWidget(down);
    tools->addStretch();
    row->addLayout(tools);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({ tr("Group"), tr("Conditions"), tr("Competitors") });
    UiHelpers::styleGrid(m_table);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->setColumnWidth(0, 220);
    m_table->setColumnWidth(2, 90);
    row->addWidget(m_table, 1);
    lay->addLayout(row, 1);

    auto* extra = new QHBoxLayout;
    // like the categories: from a file or another event; saved to a file
    auto* load = UiHelpers::button(QStringLiteral("folder"), tr("Load"), this);
    auto* loadMenu = new QMenu(load);
    loadMenu->addAction(tr("Load from a File..."), this, &OutputGroupsDialog::loadFromFile);
    loadMenu->addAction(tr("Load from an Event..."), this, &OutputGroupsDialog::loadFromEvent);
    load->setMenu(loadMenu);
    load->setMinimumWidth(110);
    auto* save = UiHelpers::button(QStringLiteral("save"), tr("Save to a File..."), this);
    extra->addWidget(load);
    extra->addWidget(save);
    extra->addSpacing(20);
    connect(save, &QPushButton::clicked, this, &OutputGroupsDialog::saveToFile);
    auto* fromField = UiHelpers::button(QStringLiteral("filter"), tr("Create the groups from a field..."), this);
    fromField->setToolTip(tr("One group for each value of a field, e.g. for each category"));
    auto* clear = UiHelpers::button(QStringLiteral("delete"), tr("Delete all"), this);
    extra->addWidget(fromField);
    extra->addWidget(clear);
    extra->addStretch();
    lay->addLayout(extra);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(add, &QToolButton::clicked, this, &OutputGroupsDialog::newGroup);
    connect(edit, &QToolButton::clicked, this, &OutputGroupsDialog::editCurrent);
    connect(del, &QToolButton::clicked, this, &OutputGroupsDialog::removeCurrent);
    connect(up, &QToolButton::clicked, this, [this] { move(-1); });
    connect(down, &QToolButton::clicked, this, [this] { move(1); });
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &OutputGroupsDialog::editCurrent);
    connect(fromField, &QPushButton::clicked, this, &OutputGroupsDialog::createFromField);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!m_groups.isEmpty()
            && QMessageBox::question(this, windowTitle(), tr("Delete all the output groups?")) == QMessageBox::Yes) {
            m_groups.clear();
            reload();
        }
    });
    connect(new QShortcut(QKeySequence(Qt::Key_F1), this), &QShortcut::activated, this, &OutputGroupsDialog::newGroup);
    connect(new QShortcut(QKeySequence(Qt::Key_F2), this), &QShortcut::activated, this, &OutputGroupsDialog::editCurrent);

    reload(0);
    resize(760, 520);
}

// File of output groups (*.ospgrp): {"outputGroups": [{"label", "any", "criteria": [[field, op, value, value2]]}]}
void OutputGroupsDialog::saveToFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/outputGroups"), AppSettings::exportDir()).toString();
    QDir().mkpath(dir);
    QString file = QFileDialog::getSaveFileName(this, tr("Save to a File"), QDir(dir).absoluteFilePath(QStringLiteral("groups.ospgrp")),
                                                tr("OpenSkiPro2 output groups (*.ospgrp)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".ospgrp"), Qt::CaseInsensitive))
        file += QStringLiteral(".ospgrp");
    AppSettings::setValue(QStringLiteral("folders/outputGroups"), QFileInfo(file).absolutePath());
    QJsonObject root;
    root[QStringLiteral("outputGroups")] = QJsonDocument::fromJson(outputGroupsToString(m_groups).toUtf8()).array();
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QJsonDocument(root).toJson()) < 0)
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1: %2").arg(file, f.errorString()));
}

void OutputGroupsDialog::loadFromFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/outputGroups"), AppSettings::exportDir()).toString();
    const QString file = QFileDialog::getOpenFileName(this, tr("Load from a File"), dir, tr("OpenSkiPro2 output groups (*.ospgrp)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("folders/outputGroups"), QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot read %1: %2").arg(file, f.errorString()));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    if (!root.value(QStringLiteral("outputGroups")).isArray()) {
        QMessageBox::warning(this, windowTitle(), tr("%1 is not a file of output groups.").arg(file));
        return;
    }
    m_groups = outputGroupsFromString(
        QString::fromUtf8(QJsonDocument(root.value(QStringLiteral("outputGroups")).toArray()).toJson(QJsonDocument::Compact)));
    reload(0);
}

void OutputGroupsDialog::loadFromEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("Load from an Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    QList<OutputGroup> groups;
    {
        Event other;
        QString err;
        if (!other.open(MainWindow::eventPath(dlg.selectedSlot()), &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        groups = other.params().outputGroups;
        other.close();
    }
    if (groups.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("The selected event has no output groups."));
        return;
    }
    m_groups = groups;
    reload(0);
}

void OutputGroupsDialog::reload(int select)
{
    m_table->setRowCount(int(m_groups.size()));
    for (int i = 0; i < m_groups.size(); ++i) {
        const OutputGroup& g = m_groups.at(i);
        int count = 0;
        for (const Competitor& c : m_ev->competitors())
            if (g.matches(c))
                ++count;
        auto* label = new QTableWidgetItem(g.label);
        auto* cond = new QTableWidgetItem(describe(m_ev, g));
        auto* n = new QTableWidgetItem(QString::number(count));
        n->setTextAlignment(Qt::AlignCenter);
        if (count == 0)
            n->setForeground(Qt::red);
        m_table->setItem(i, 0, label);
        m_table->setItem(i, 1, cond);
        m_table->setItem(i, 2, n);
    }
    if (select >= 0 && select < m_table->rowCount())
        m_table->selectRow(select);
}

void OutputGroupsDialog::newGroup()
{
    OutputGroup g;
    g.criteria << GroupCriterion{};
    if (!editGroup(m_ev, g, this))
        return;
    const int at = m_table->currentRow() >= 0 ? m_table->currentRow() + 1 : int(m_groups.size());
    m_groups.insert(at, g);
    reload(at);
}

void OutputGroupsDialog::editCurrent()
{
    const int r = m_table->currentRow();
    if (r < 0 || r >= m_groups.size())
        return;
    OutputGroup g = m_groups.at(r);
    if (editGroup(m_ev, g, this)) {
        m_groups[r] = g;
        reload(r);
    }
}

void OutputGroupsDialog::removeCurrent()
{
    const int r = m_table->currentRow();
    if (r < 0 || r >= m_groups.size())
        return;
    m_groups.removeAt(r);
    reload(qMin(r, int(m_groups.size()) - 1));
}

void OutputGroupsDialog::move(int delta)
{
    const int r = m_table->currentRow();
    const int t = r + delta;
    if (r < 0 || t < 0 || t >= m_groups.size())
        return;
    m_groups.swapItemsAt(r, t);
    reload(t);
}

void OutputGroupsDialog::createFromField()
{
    QStringList names;
    QList<int> ids;
    for (const auto& def : competitorFieldDefs()) {
        if (def.id == CompField::Name || def.id == CompField::Bib || !m_ev->params().fields.isActive(def.id))
            continue;
        names << fieldLabel(m_ev, def.id);
        ids << def.id;
    }
    bool ok = false;
    const QString sel = QInputDialog::getItem(this, tr("Create the groups from a field"), tr("Field :"), names,
                                              qMax(0, int(ids.indexOf(CompField::Category))), false, &ok);
    if (!ok)
        return;
    const int field = ids.value(int(names.indexOf(sel)));
    // categories in the order of their definition, then the other values alphabetically
    QStringList values;
    if (field == CompField::Category)
        for (const CategoryDef& c : m_ev->categories().categories)
            if (!c.name.isEmpty() && !values.contains(c.name))
                values << c.name;
    QStringList others;
    for (const Competitor& c : m_ev->competitors()) {
        const QString v = c.value(field);
        if (!v.isEmpty() && !values.contains(v) && !others.contains(v))
            others << v;
    }
    std::sort(others.begin(), others.end(), [](const QString& a, const QString& b) { return QString::localeAwareCompare(a, b) < 0; });
    values += others;
    for (const QString& v : values) {
        OutputGroup g;
        g.label = v;
        g.criteria << GroupCriterion{ field, GroupCriterion::Equal, v, QString() };
        m_groups << g;
    }
    reload(int(m_groups.size()) - int(values.size()));
}

void OutputGroupsDialog::accept()
{
    EventParams p = m_ev->params();
    p.outputGroups = m_groups;
    m_ev->setParams(p);
    QDialog::accept();
}

bool OutputGroupsDialog::editGroup(const Event* ev, OutputGroup& g, QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(tr("Output group"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* form = new QFormLayout;
    auto* label = new QLineEdit(g.label, &dlg);
    form->addRow(tr("Name of the group :"), label);
    auto* any = new QComboBox(&dlg);
    any->addItem(tr("All the conditions must be met"), false);
    any->addItem(tr("One of the conditions is enough"), true);
    any->setCurrentIndex(g.anyMode ? 1 : 0);
    form->addRow(QString(), any);
    lay->addLayout(form);
    lay->addWidget(UiHelpers::titleBar(tr("Conditions"), &dlg));

    struct Row {
        QComboBox* field;
        QComboBox* op;
        QLineEdit* v1;
        QLineEdit* v2;
    };
    QList<Row> rows;
    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("Field"), &dlg), 0, 0);
    grid->addWidget(new QLabel(tr("Condition"), &dlg), 0, 1);
    grid->addWidget(new QLabel(tr("Value"), &dlg), 0, 2);
    grid->addWidget(new QLabel(tr("To (between)"), &dlg), 0, 3);
    for (int i = 0; i < kCriteriaRows; ++i) {
        const bool used = i < g.criteria.size();
        const GroupCriterion c = used ? g.criteria.at(i) : GroupCriterion{};
        Row r{ new QComboBox(&dlg), new QComboBox(&dlg), new QLineEdit(c.value, &dlg), new QLineEdit(c.value2, &dlg) };
        r.field->addItem(tr("(none)"), 0);
        for (const auto& def : competitorFieldDefs())
            if (def.id != CompField::Name)
                r.field->addItem(fieldLabel(ev, def.id), def.id);
        r.field->setCurrentIndex(used ? qMax(0, r.field->findData(c.field)) : 0);
        for (int op : { GroupCriterion::Equal, GroupCriterion::NotEqual, GroupCriterion::BeginsWith, GroupCriterion::Contains,
                        GroupCriterion::Between, GroupCriterion::Greater, GroupCriterion::Less })
            r.op->addItem(groupOperatorText(op), op);
        r.op->setCurrentIndex(qMax(0, r.op->findData(c.op)));
        auto sync = [r] {
            const bool on = r.field->currentData().toInt() > 0;
            r.op->setEnabled(on);
            r.v1->setEnabled(on);
            r.v2->setEnabled(on && r.op->currentData().toInt() == GroupCriterion::Between);
        };
        QObject::connect(r.field, &QComboBox::currentIndexChanged, &dlg, sync);
        QObject::connect(r.op, &QComboBox::currentIndexChanged, &dlg, sync);
        sync();
        grid->addWidget(r.field, i + 1, 0);
        grid->addWidget(r.op, i + 1, 1);
        grid->addWidget(r.v1, i + 1, 2);
        grid->addWidget(r.v2, i + 1, 3);
        rows << r;
    }
    grid->setColumnStretch(2, 1);
    grid->setColumnStretch(3, 1);
    lay->addLayout(grid);
    auto* count = UiHelpers::infoBox(&dlg);
    lay->addWidget(count);
    lay->addLayout(UiHelpers::okCancelRow(&dlg));

    auto read = [&] {
        OutputGroup out;
        out.label = label->text().trimmed();
        out.anyMode = any->currentData().toBool();
        for (const Row& r : rows) {
            const int field = r.field->currentData().toInt();
            if (field <= 0)
                continue;
            GroupCriterion c;
            c.field = field;
            c.op = r.op->currentData().toInt();
            c.value = r.v1->text().trimmed();
            c.value2 = c.op == GroupCriterion::Between ? r.v2->text().trimmed() : QString();
            out.criteria << c;
        }
        return out;
    };
    auto updateCount = [&] {
        const OutputGroup cur = read();
        int n = 0;
        for (const Competitor& c : ev->competitors())
            if (cur.matches(c))
                ++n;
        count->setText(tr("%n competitor(s) in the group", nullptr, n));
    };
    for (const Row& r : rows) {
        QObject::connect(r.field, &QComboBox::currentIndexChanged, &dlg, updateCount);
        QObject::connect(r.op, &QComboBox::currentIndexChanged, &dlg, updateCount);
        QObject::connect(r.v1, &QLineEdit::textChanged, &dlg, updateCount);
        QObject::connect(r.v2, &QLineEdit::textChanged, &dlg, updateCount);
    }
    QObject::connect(any, &QComboBox::currentIndexChanged, &dlg, updateCount);
    updateCount();
    // a new group named after the value of its first condition
    QObject::connect(rows.first().v1, &QLineEdit::textEdited, &dlg, [&](const QString& v) {
        if (g.label.isEmpty())
            label->setText(v);
    });

    while (dlg.exec() == QDialog::Accepted) {
        const OutputGroup out = read();
        if (out.label.isEmpty()) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), tr("Enter the name of the group."));
            continue;
        }
        g = out;
        return true;
    }
    return false;
}

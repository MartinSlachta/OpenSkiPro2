#include "AssignBibsDialog.h"
#include "DoubleDrawDialog.h"
#include "app/AppSettings.h"
#include "core/BibDraw.h"
#include "core/Event.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const char* const kNoCategory = "\x01"; // data of the "(no category)" item
}

AssignBibsDialog::AssignBibsDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
{
    setWindowTitle(tr("Assign Bibs"));
    auto* lay = new QVBoxLayout(this);

    // competitors concerned (booklet 15.1: gender / categories of the competition)
    auto* who = new QGroupBox(tr("Competitors"), this);
    auto* wl = new QFormLayout(who);
    m_gender = new QComboBox(who);
    m_gender->addItem(tr("All"), int(Gender::Unknown));
    m_gender->addItem(tr("Men"), int(Gender::Male));
    m_gender->addItem(tr("Women"), int(Gender::Female));
    wl->addRow(tr("Gender :"), m_gender);
    QStringList cats;
    bool noCategory = false;
    for (const auto& c : ev->competitors()) {
        if (c.category.isEmpty())
            noCategory = true;
        else if (!cats.contains(c.category))
            cats << c.category;
    }
    // order of the category grid, then the others
    QStringList ordered;
    for (const auto& d : ev->categories().categories)
        if (cats.contains(d.name) && !ordered.contains(d.name))
            ordered << d.name;
    cats.sort();
    for (const QString& c : cats)
        if (!ordered.contains(c))
            ordered << c;
    m_categories = new QListWidget(who);
    for (const QString& c : ordered) {
        auto* it = new QListWidgetItem(c, m_categories);
        it->setData(Qt::UserRole, c);
        it->setCheckState(Qt::Checked);
    }
    if (noCategory && !ordered.isEmpty()) {
        auto* it = new QListWidgetItem(tr("(no category)"), m_categories);
        it->setData(Qt::UserRole, QString::fromLatin1(kNoCategory));
        it->setCheckState(Qt::Checked);
    }
    m_categories->setMaximumHeight(110);
    if (m_categories->count() > 0)
        wl->addRow(tr("Categories :"), m_categories);
    else
        m_categories->hide();
    m_onlyMissing = new QCheckBox(tr("Only competitors without a bib"), who);
    wl->addRow(QString(), m_onlyMissing);
    m_count = new QLabel(who);
    wl->addRow(QString(), m_count);
    lay->addWidget(who);

    // start order
    auto* how = new QGroupBox(tr("Start order"), this);
    auto* hl = new QFormLayout(how);
    m_order = new QComboBox(how);
    m_order->addItem(tr("Points (best first)"), int(Mode::Points));
    m_order->addItem(tr("According to FIS rules - X best (draw)"), int(Mode::BestDraw));
    m_order->addItem(tr("According to FIS rules - X best (reversed)"), int(Mode::BestReversed));
    m_order->addItem(tr("Double draw NC / NJC (1-7, 8-15)"), int(Mode::DoubleDraw));
    m_order->addItem(tr("Name"), int(Mode::Name));
    m_order->addItem(tr("Random draw"), int(Mode::Random));
    m_order->addItem(tr("Current bib order"), int(Mode::CurrentBibs));
    m_order->setCurrentIndex(qMax(0, m_order->findData(AppSettings::value(QStringLiteral("assignBibs/mode"), int(Mode::BestDraw)).toInt())));
    hl->addRow(tr("Order :"), m_order);
    m_points = new QComboBox(how);
    for (int i = 0; i < kDisciplineCount; ++i)
        m_points->addItem(disciplineName(Discipline(i)), i);
    m_points->setCurrentIndex(int(ev->params().type.discipline));
    m_points->setToolTip(tr("FIS points used for the order (ICR 621.6: Alpine Combined at NC / NJC may be seeded by DH or SG points)"));
    hl->addRow(tr("Points of :"), m_points);
    m_best = new QSpinBox(how);
    m_best->setRange(1, 999);
    m_best->setValue(AppSettings::value(QStringLiteral("assignBibs/best"), 15).toInt());
    m_bestLabel = new QLabel(how);
    hl->addRow(m_bestLabel, m_best);
    m_group = new QLabel(how);
    m_group->setWordWrap(true);
    hl->addRow(QString(), m_group);
    lay->addWidget(how);

    // bibs
    auto* bibs = new QGroupBox(tr("Bibs"), this);
    auto* bl = new QFormLayout(bibs);
    m_first = new QSpinBox(bibs);
    m_first->setRange(1, 9999);
    m_first->setValue(1);
    bl->addRow(tr("First bib :"), m_first);
    m_skip = new QLineEdit(bibs);
    m_skip->setPlaceholderText(tr("e.g. 13, 27, 40-42"));
    m_skip->setToolTip(tr("Bibs missing (not returned): they are skipped"));
    bl->addRow(tr("Bibs to skip :"), m_skip);
    m_bibs = new QLabel(bibs);
    m_bibs->setWordWrap(true);
    bl->addRow(QString(), m_bibs);
    lay->addWidget(bibs);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(m_gender, &QComboBox::currentIndexChanged, this, &AssignBibsDialog::updateState);
    connect(m_categories, &QListWidget::itemChanged, this, &AssignBibsDialog::updateState);
    connect(m_onlyMissing, &QCheckBox::toggled, this, &AssignBibsDialog::updateState);
    connect(m_order, &QComboBox::currentIndexChanged, this, &AssignBibsDialog::updateState);
    connect(m_points, &QComboBox::currentIndexChanged, this, &AssignBibsDialog::updateState);
    connect(m_best, &QSpinBox::valueChanged, this, &AssignBibsDialog::updateState);
    connect(m_first, &QSpinBox::valueChanged, this, &AssignBibsDialog::updateState);
    connect(m_skip, &QLineEdit::textChanged, this, &AssignBibsDialog::updateState);
    updateState();
}

AssignBibsDialog::Mode AssignBibsDialog::mode() const
{
    return Mode(m_order->currentData().toInt());
}

Discipline AssignBibsDialog::discipline() const
{
    return Discipline(m_points->currentData().toInt());
}

QVector<Competitor> AssignBibsDialog::selected() const
{
    const Gender g = Gender(m_gender->currentData().toInt());
    QSet<QString> cats;
    bool filterCats = false;
    for (int i = 0; i < m_categories->count(); ++i) {
        const QListWidgetItem* it = m_categories->item(i);
        if (it->checkState() == Qt::Checked)
            cats.insert(it->data(Qt::UserRole).toString());
        else
            filterCats = true;
    }
    QVector<Competitor> out;
    for (const auto& c : m_ev->competitors()) {
        if (g != Gender::Unknown && c.gender != g)
            continue;
        if (filterCats && !cats.contains(c.category.isEmpty() ? QString::fromLatin1(kNoCategory) : c.category))
            continue;
        if (m_onlyMissing->isChecked() && c.bib > 0)
            continue;
        out << c;
    }
    return out;
}

void AssignBibsDialog::updateState()
{
    const Mode m = mode();
    const bool usesPoints = m == Mode::Points || m == Mode::BestDraw || m == Mode::BestReversed || m == Mode::DoubleDraw;
    m_points->setEnabled(usesPoints);
    const bool best = m == Mode::BestDraw || m == Mode::BestReversed;
    m_best->setEnabled(best);
    m_bestLabel->setEnabled(best);
    m_bestLabel->setText(m == Mode::BestReversed ? tr("Number of reversed competitors :")
                                                 : tr("Number of competitors for the draw :"));

    const QVector<Competitor> list = selected();
    const Discipline d = discipline();
    int withPoints = 0;
    for (const auto& c : list)
        if (c.pointsFor(d) >= 0)
            ++withPoints;
    m_count->setText(usesPoints ? tr("%n competitor(s), %1 with points", nullptr, int(list.size())).arg(withPoints)
                                : tr("%n competitor(s)", nullptr, int(list.size())));

    // size of the first group (ties at the limit included), without drawing
    QRandomGenerator rng(1);
    const QVector<int> order = BibDraw::byPoints(list, d, rng);
    QString group;
    if (best) {
        const int n = BibDraw::firstGroupSize(list, order, d, m_best->value());
        group = m == Mode::BestDraw ? tr("Drawn group: %n competitor(s)", nullptr, n)
                                    : tr("Reversed group: %n competitor(s)", nullptr, n);
        if (n > qMin(m_best->value(), withPoints))
            group += QStringLiteral(" - ") + tr("equal points at rank %1").arg(m_best->value());
        group += QStringLiteral(". ") + tr("Then by points, without points drawn at the end.");
    } else if (m == Mode::DoubleDraw) {
        const BibDraw::DoubleDrawGroups g = BibDraw::doubleDrawGroups(list, order, d);
        group = tr("Groups 1-%1 and %2-%3 (start orders typed or drawn in the next window), then by points, "
                   "without points drawn at the end.")
                    .arg(g.firstEnd)
                    .arg(g.firstEnd + 1)
                    .arg(g.secondEnd);
        if (g.tieAtLimit)
            group += QStringLiteral(" ") + tr("Equal points across the limit of a group (the group can be increased in the next window)!");
    } else if (m == Mode::Points) {
        group = tr("Equal points drawn, without points drawn at the end.");
    }
    m_group->setText(group);
    m_group->setVisible(!group.isEmpty());

    bool ok = true;
    const QSet<int> skip = BibDraw::parseBibs(m_skip->text(), &ok);
    if (!ok) {
        m_bibs->setText(QStringLiteral("<span style='color:#c00000'>%1</span>")
                            .arg(tr("Bibs to skip: numbers separated by commas, ranges with '-'").toHtmlEscaped()));
        return;
    }
    const QVector<int> nums = BibDraw::bibNumbers(m_first->value(), int(list.size()), skip);
    if (nums.isEmpty()) {
        m_bibs->clear();
        return;
    }
    QString text = tr("Bibs %1 to %2").arg(nums.first()).arg(nums.last());
    QSet<int> skipped;
    for (int b : skip)
        if (b > nums.first() && b < nums.last())
            skipped.insert(b);
    if (!skipped.isEmpty())
        text += QStringLiteral(", ") + tr("without %1").arg(BibDraw::formatBibs(skipped));
    m_bibs->setText(text.toHtmlEscaped());
}

void AssignBibsDialog::accept()
{
    QVector<Competitor> list = selected();
    if (list.isEmpty()) {
        QMessageBox::information(this, windowTitle(), tr("No competitor to number."));
        return;
    }
    bool ok = true;
    QSet<int> skip = BibDraw::parseBibs(m_skip->text(), &ok);
    if (!ok) {
        QMessageBox::warning(this, windowTitle(), tr("Bibs to skip: numbers separated by commas, ranges with '-'"));
        return;
    }
    const Discipline d = discipline();
    QRandomGenerator& rng = *QRandomGenerator::global();
    QVector<int> order;
    switch (mode()) {
    case Mode::Points:
        order = BibDraw::byPoints(list, d, rng);
        break;
    case Mode::BestDraw:
        order = BibDraw::bestDraw(list, d, m_best->value(), rng);
        break;
    case Mode::BestReversed:
        order = BibDraw::bestReversed(list, d, m_best->value(), rng);
        break;
    case Mode::DoubleDraw: {
        const QVector<int> byPoints = BibDraw::byPoints(list, d, rng);
        if (BibDraw::doubleDrawGroups(list, byPoints, d).secondEnd == 0) {
            QMessageBox::information(this, windowTitle(), tr("No competitor with %1 points.").arg(disciplineName(d)));
            return;
        }
        DoubleDrawDialog dd(list, byPoints, d, this);
        if (dd.exec() != QDialog::Accepted)
            return;
        order = BibDraw::applyStartOrders(dd.order(), dd.startOrders());
        break;
    }
    case Mode::Name:
        for (int i = 0; i < list.size(); ++i)
            order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return QString::localeAwareCompare(list.at(a).fullName(), list.at(b).fullName()) < 0;
        });
        break;
    case Mode::Random:
        for (int i = 0; i < list.size(); ++i)
            order << i;
        std::shuffle(order.begin(), order.end(), rng);
        break;
    case Mode::CurrentBibs:
        for (int i = 0; i < list.size(); ++i)
            order << i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return (list.at(a).bib <= 0 ? INT_MAX : list.at(a).bib) < (list.at(b).bib <= 0 ? INT_MAX : list.at(b).bib);
        });
        break;
    }

    int first = m_first->value();
    QSet<int> selectedIds;
    for (const auto& c : list)
        selectedIds.insert(c.id);
    if (m_onlyMissing->isChecked())
        for (const auto& c : m_ev->competitors())
            if (c.bib > 0)
                first = qMax(first, c.bib + 1);
    const QVector<int> nums = BibDraw::bibNumbers(first, int(order.size()), skip);

    // bibs kept by the other competitors
    QHash<int, QString> used;
    for (const auto& c : m_ev->competitors())
        if (!selectedIds.contains(c.id) && c.bib > 0)
            used.insert(c.bib, c.fullName());
    QStringList clashes;
    for (int b : nums)
        if (used.contains(b))
            clashes << QStringLiteral("%1 %2").arg(b).arg(used.value(b));
    if (!clashes.isEmpty()) {
        QString text = tr("These bibs are already used by other competitors:") + QStringLiteral("\n\n")
            + QStringList(clashes.mid(0, 15)).join(QLatin1Char('\n'));
        if (clashes.size() > 15)
            text += QStringLiteral("\n...");
        text += QStringLiteral("\n\n") + tr("Assign the bibs anyway?");
        if (QMessageBox::warning(this, windowTitle(), text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
            != QMessageBox::Yes)
            return;
    }

    AppSettings::setValue(QStringLiteral("assignBibs/mode"), int(mode()));
    AppSettings::setValue(QStringLiteral("assignBibs/best"), m_best->value());
    m_ev->beginBatch();
    for (int i = 0; i < order.size(); ++i) {
        Competitor c = list.at(order.at(i));
        c.bib = nums.at(i);
        m_ev->updateCompetitor(c);
    }
    m_ev->commitBatch();
    QDialog::accept();
}

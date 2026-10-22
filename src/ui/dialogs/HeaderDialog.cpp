#include "HeaderDialog.h"
#include "ui/UiHelpers.h"

#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

QLineEdit* HeaderDialog::edit(const QString& key, int width)
{
    auto* e = new QLineEdit(m_params.header.value(key), this);
    if (width > 0)
        e->setMaximumWidth(width);
    m_edits.insert(key, e);
    return e;
}

QComboBox* HeaderDialog::choice(const QString& key, const QStringList& values)
{
    auto* c = new QComboBox(this);
    c->setEditable(true);
    c->addItem(QString());
    c->addItems(values);
    c->setCurrentText(m_params.header.value(key));
    m_edits.insert(key, c->lineEdit());
    return c;
}

HeaderDialog::HeaderDialog(const EventParams& p, QWidget* parent)
    : QDialog(parent)
    , m_params(p)
{
    setWindowTitle(tr("Header"));
    auto* lay = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    // jury
    auto* jury = new QGridLayout;
    jury->addWidget(UiHelpers::titleBar(tr("COMPETITION JURY"), this), 0, 0, 1, 4);
    const QList<QPair<QString, QString>> roles = {
        { QLatin1String(HeaderKeys::TechnicalDelegate), tr("TECHNICAL DELEGATE") },
        { QLatin1String(HeaderKeys::ChiefOfRace), tr("CHIEF OF RACE") },
        { QLatin1String(HeaderKeys::Referee), tr("REFEREE") },
        { QLatin1String(HeaderKeys::AssistantReferee), tr("ASSISTANT REFEREE") },
        { QLatin1String(HeaderKeys::CourseChief), tr("COURSE CHIEF") },
        { QLatin1String(HeaderKeys::StartReferee), tr("START REFEREE") },
        { QLatin1String(HeaderKeys::FinishReferee), tr("FINISH REFEREE") },
    };
    int r = 1;
    for (const auto& role : roles) {
        jury->addWidget(new QLabel(role.second, this), r, 0);
        auto* code = edit(role.first + QStringLiteral(".code"), 70);
        code->setPlaceholderText(tr("Code"));
        jury->addWidget(code, r, 1);
        jury->addWidget(edit(role.first + QStringLiteral(".name")), r, 2);
        auto* n = edit(role.first + QStringLiteral(".nation"), 45);
        n->setPlaceholderText(tr("Nat."));
        jury->addWidget(n, r, 3);
        ++r;
    }
    jury->setColumnStretch(2, 1);
    top->addLayout(jury, 3);

    top->addStretch(1);
    lay->addLayout(top);

    auto* line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    lay->addWidget(line);

    // runs: columns 2-4 run 1, 5 the arrows copying a row of run 1 to run 2, 6-8 run 2
    auto* runs = new QGridLayout;
    QList<QStringList> rowKeys; // keys of each row without the "run<i>." prefix
    auto personKeys = [](const QString& key) {
        return QStringList{ key + QStringLiteral(".code"), key + QStringLiteral(".name"), key + QStringLiteral(".nation") };
    };
    rowKeys << personKeys(QStringLiteral("setter"));
    for (int i = 0; i < 5; ++i)
        rowKeys << personKeys(QStringLiteral("fore") + QLatin1Char(char('A' + i)));
    for (const auto& d : courseKeys())
        rowKeys << QStringList{ QLatin1String(d.first) };
    rowKeys << QStringList{ QStringLiteral("gates") } << QStringList{ QStringLiteral("directions") }
            << QStringList{ QStringLiteral("starttime") };
    const int firstCourseRow = 1 + 6 + 1; // after the course setter, the forerunners and a gap row

    for (int run = 1; run <= 2; ++run) {
        const int c0 = run == 1 ? 2 : 6;
        runs->addWidget(UiHelpers::titleBar(run == 1 ? tr("1st RUN") : tr("2nd RUN"), this), 0, c0, 1, 3);
        const QString prefix = QStringLiteral("run%1.").arg(run);
        auto person = [&](int row, const QString& key) {
            auto* code = edit(prefix + key + QStringLiteral(".code"), 70);
            code->setPlaceholderText(tr("Code"));
            runs->addWidget(code, row, c0);
            runs->addWidget(edit(prefix + key + QStringLiteral(".name")), row, c0 + 1);
            auto* n = edit(prefix + key + QStringLiteral(".nation"), 45);
            n->setPlaceholderText(tr("Nat."));
            runs->addWidget(n, row, c0 + 2);
        };
        person(1, QStringLiteral("setter"));
        for (int i = 0; i < 5; ++i)
            person(2 + i, QStringLiteral("fore") + QLatin1Char(char('A' + i)));
        // course data of the run (each run may be on another course)
        int row = firstCourseRow;
        for (const auto& d : courseKeys()) {
            const QString key = prefix + QLatin1String(d.first);
            auto* e = edit(key, d.second ? 80 : 0);
            if (e->text().isEmpty()) // event of an older version: one course for all the runs
                e->setText(m_params.header.runValue(run, QLatin1String(d.first)));
            if (d.second) {
                auto* h = new QHBoxLayout;
                h->addWidget(e);
                h->addWidget(new QLabel(QStringLiteral("m"), this));
                h->addStretch();
                runs->addLayout(h, row, c0 + 1);
            } else {
                runs->addWidget(e, row, c0 + 1);
            }
            ++row;
        }
        runs->addWidget(edit(prefix + QStringLiteral("gates"), 70), row++, c0 + 1);
        runs->addWidget(edit(prefix + QStringLiteral("directions"), 70), row++, c0 + 1);
        runs->addWidget(edit(prefix + QStringLiteral("starttime"), 70), row++, c0 + 1);

        // the drop is the difference of the start and finish altitudes
        QLineEdit* start = m_edits.value(prefix + QLatin1String(HeaderKeys::StartAltitude));
        QLineEdit* finish = m_edits.value(prefix + QLatin1String(HeaderKeys::FinishAltitude));
        QLineEdit* drop = m_edits.value(prefix + QLatin1String(HeaderKeys::Drop));
        auto updateDrop = [start, finish, drop] {
            const QString d = HeaderParams::dropOf(start->text(), finish->text());
            if (!d.isEmpty())
                drop->setText(d);
        };
        connect(start, &QLineEdit::textChanged, this, updateDrop);
        connect(finish, &QLineEdit::textChanged, this, updateDrop);
        drop->setToolTip(tr("Start - finish, calculated"));
    }

    auto copyRow = [this](const QStringList& keys) {
        for (const QString& k : keys) {
            QLineEdit* from = m_edits.value(QStringLiteral("run1.") + k);
            QLineEdit* to = m_edits.value(QStringLiteral("run2.") + k);
            if (from && to)
                to->setText(from->text());
        }
    };
    auto arrow = [this](const QString& tip) {
        auto* b = new QToolButton(this);
        b->setIcon(UiHelpers::icon(QStringLiteral("right")));
        b->setAutoRaise(true);
        b->setToolTip(tip);
        return b;
    };
    auto* all = arrow(tr("Copies all the data of run 1 to run 2"));
    connect(all, &QToolButton::clicked, this, [copyRow, rowKeys] {
        for (const QStringList& k : rowKeys)
            copyRow(k);
    });
    runs->addWidget(all, 0, 5, Qt::AlignCenter);
    for (int i = 0; i < rowKeys.size(); ++i) {
        const int row = i < 6 ? 1 + i : firstCourseRow + (i - 6);
        auto* b = arrow(tr("Copies this line of run 1 to run 2"));
        const QStringList keys = rowKeys.at(i);
        connect(b, &QToolButton::clicked, this, [copyRow, keys] { copyRow(keys); });
        runs->addWidget(b, row, 5, Qt::AlignCenter);
    }

    runs->addWidget(new QLabel(tr("COURSESETTER"), this), 1, 0);
    runs->addWidget(new QLabel(tr("FORERUNNERS"), this), 2, 0);
    for (int i = 0; i < 5; ++i) {
        auto* l = new QLabel(QStringLiteral("- %1 -").arg(QLatin1Char(char('A' + i))), this);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        runs->addWidget(l, 2 + i, 1);
    }
    runs->setRowMinimumHeight(7, 6);
    const QStringList labels = { tr("COURSE"), tr("START"), tr("FINISH"), tr("DROP"), tr("LENGTH"), tr("HOMOLOGATION"),
                                 tr("GATES"), tr("DIRECTIONS"), tr("STARTING TIME") };
    for (int i = 0; i < labels.size(); ++i)
        runs->addWidget(new QLabel(labels.at(i), this), firstCourseRow + i, 0);
    runs->setColumnStretch(3, 1);
    runs->setColumnStretch(7, 1);
    lay->addLayout(runs);

    auto* line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    lay->addWidget(line2);

    auto* weather = new QHBoxLayout;
    // the values of the FIS live timing description (meteo codes), another text can be typed
    const QStringList weatherValues = { QStringLiteral("Sunny"),   QStringLiteral("Partially cloudy"), QStringLiteral("Cloudy"),
                                        QStringLiteral("Snowing"), QStringLiteral("Raining"),          QStringLiteral("Foggy"),
                                        QStringLiteral("Clear night") };
    const QStringList snowValues = { QStringLiteral("Fine grained"), QStringLiteral("Grip"),
                                     QStringLiteral("Granular"),     QStringLiteral("Hard"),
                                     QStringLiteral("Hard packed"),  QStringLiteral("Hard packed variable"),
                                     QStringLiteral("Icy"),          QStringLiteral("Packed"),
                                     QStringLiteral("Packed powder"), QStringLiteral("Powder"),
                                     QStringLiteral("Soft"),         QStringLiteral("Spring conditions"),
                                     QStringLiteral("Wet"),          QStringLiteral("Wet & powder") };
    weather->addWidget(new QLabel(tr("WEATHER"), this));
    weather->addWidget(choice(QLatin1String(HeaderKeys::Weather), weatherValues), 2);
    weather->addWidget(new QLabel(tr("SNOW"), this));
    weather->addWidget(choice(QLatin1String(HeaderKeys::Snow), snowValues), 2);
    weather->addWidget(new QLabel(tr("TEMPERATURE S"), this));
    weather->addWidget(edit(QLatin1String(HeaderKeys::TempStart), 50));
    weather->addWidget(new QLabel(tr("F"), this));
    weather->addWidget(edit(QLatin1String(HeaderKeys::TempFinish), 50));
    weather->addWidget(new QLabel(tr("LIST n°"), this));
    weather->addWidget(edit(QLatin1String(HeaderKeys::ListNumber), 70));
    lay->addLayout(weather);

    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(980, 0);
}

QList<QPair<const char*, bool>> HeaderDialog::courseKeys()
{
    return { { HeaderKeys::Course, false },        { HeaderKeys::StartAltitude, true }, { HeaderKeys::FinishAltitude, true },
             { HeaderKeys::Drop, true },           { HeaderKeys::Length, true },        { HeaderKeys::Homologation, false } };
}

void HeaderDialog::apply(EventParams& p) const
{
    // the course data are kept per run: the common values of older versions are not used any more
    for (const auto& d : courseKeys())
        p.header.values.remove(QLatin1String(d.first));
    for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it) {
        const QString v = it.value()->text().trimmed();
        if (v.isEmpty())
            p.header.values.remove(it.key());
        else
            p.header.setValue(it.key(), v);
    }
}

#include "PrintingParametersDialog.h"
#include "LogoPositionsDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFontDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

static QFont* fontRef(PrintingParams& pp, int i)
{
    QFont* fonts[] = { &pp.fontTitle1, &pp.fontTitle2, &pp.fontDocTitle, &pp.fontFooter, &pp.fontLabels, &pp.fontLines };
    return fonts[i];
}

PrintingParametersDialog::PrintingParametersDialog(Event* ev, QWidget* parent)
    : QDialog(parent)
    , m_ev(ev)
    , m_pp(ev->params().printing)
{
    setWindowTitle(tr("Printing Parameters"));
    auto* lay = new QVBoxLayout(this);

    // fonts
    auto* fontsRow = new QHBoxLayout;
    auto* fonts = new QGroupBox(tr("Fonts"), this);
    auto* fg = new QGridLayout(fonts);
    const QStringList names = { tr("Title 1 :"), tr("Title 2 :"), tr("Document Title :"), tr("Foot of Page :") };
    for (int i = 0; i < 4; ++i) {
        fg->addWidget(new QLabel(names.at(i), fonts), i, 0, Qt::AlignRight);
        m_fontButtons[size_t(i)] = new QPushButton(fonts);
        m_fontButtons[size_t(i)]->setMinimumWidth(190);
        fg->addWidget(m_fontButtons[size_t(i)], i, 1);
    }
    m_showTitle1 = new QCheckBox(fonts);
    m_showTitle1->setChecked(m_pp.showTitle1);
    m_showTitle1->setToolTip(tr("Print title 1"));
    m_showTitle2 = new QCheckBox(fonts);
    m_showTitle2->setChecked(m_pp.showTitle2);
    m_showTitle2->setToolTip(tr("Print title 2"));
    fg->addWidget(m_showTitle1, 0, 2);
    fg->addWidget(m_showTitle2, 1, 2);
    fontsRow->addWidget(fonts, 3);
    auto* tab = new QGroupBox(tr("Tab"), this);
    auto* tg = new QGridLayout(tab);
    tg->addWidget(new QLabel(tr("Labels :"), tab), 0, 0, Qt::AlignRight);
    tg->addWidget(new QLabel(tr("Lines :"), tab), 1, 0, Qt::AlignRight);
    for (int i = 4; i < 6; ++i) {
        m_fontButtons[size_t(i)] = new QPushButton(tab);
        m_fontButtons[size_t(i)]->setMinimumWidth(170);
        tg->addWidget(m_fontButtons[size_t(i)], i - 4, 1);
    }
    m_autoAdjust = new QCheckBox(tr("Automatically adjust"), tab);
    m_autoAdjust->setChecked(m_pp.autoAdjust);
    tg->addWidget(m_autoAdjust, 2, 1);
    fontsRow->addWidget(tab, 2);
    lay->addLayout(fontsRow);
    for (int i = 0; i < 6; ++i)
        connect(m_fontButtons[size_t(i)], &QPushButton::clicked, this, [this, i] {
            bool ok = false;
            const QFont f = QFontDialog::getFont(&ok, *fontRef(m_pp, i), this);
            if (ok) {
                *fontRef(m_pp, i) = f;
                updateFontButtons();
            }
        });
    updateFontButtons();

    // display
    auto* display = new QGroupBox(tr("Display"), this);
    auto* dl = new QHBoxLayout(display);
    m_asTable = new QCheckBox(tr("Show as Table"), display);
    m_asTable->setChecked(m_pp.showAsTable);
    m_grayRows = new QCheckBox(tr("Grayed Rows"), display);
    m_grayRows->setChecked(m_pp.grayRows);
    m_grayLevel = new QSpinBox(display);
    m_grayLevel->setRange(0, 60);
    m_grayLevel->setSuffix(QStringLiteral(" %"));
    m_grayLevel->setValue(m_pp.grayLevel);
    dl->addWidget(m_asTable);
    dl->addStretch();
    dl->addWidget(m_grayRows);
    dl->addWidget(new QLabel(tr("Gray Level :"), display));
    dl->addWidget(m_grayLevel);
    lay->addWidget(display);

    // margins
    auto* margins = new QGroupBox(tr("Margins (mm)"), this);
    auto* mg = new QGridLayout(margins);
    const QStringList cols = { tr("Top"), tr("Bottom"), tr("Left"), tr("Right"), tr("Titles - Alignment") };
    for (int i = 0; i < cols.size(); ++i)
        mg->addWidget(new QLabel(cols.at(i), margins), 0, i + 1, Qt::AlignCenter);
    mg->addWidget(new QLabel(tr("1st Page :"), margins), 1, 0, Qt::AlignRight);
    mg->addWidget(new QLabel(tr("Following Pages :"), margins), 2, 0, Qt::AlignRight);
    for (int i = 0; i < 4; ++i) {
        m_marginFirst[size_t(i)] = new QSpinBox(margins);
        m_marginOther[size_t(i)] = new QSpinBox(margins);
        for (auto* s : { m_marginFirst[size_t(i)], m_marginOther[size_t(i)] })
            s->setRange(0, 100);
        m_marginFirst[size_t(i)]->setValue(m_pp.marginsFirst[size_t(i)]);
        m_marginOther[size_t(i)]->setValue(m_pp.marginsOther[size_t(i)]);
        mg->addWidget(m_marginFirst[size_t(i)], 1, i + 1);
        mg->addWidget(m_marginOther[size_t(i)], 2, i + 1);
    }
    const QStringList aligns = { tr("Left"), tr("Center"), tr("Right") };
    m_alignFirst = new QComboBox(margins);
    m_alignFirst->addItems(aligns);
    m_alignFirst->setCurrentIndex(qBound(0, m_pp.titleAlignFirst, 2));
    m_alignOther = new QComboBox(margins);
    m_alignOther->addItems(aligns);
    m_alignOther->setCurrentIndex(qBound(0, m_pp.titleAlignOther, 2));
    mg->addWidget(m_alignFirst, 1, 5);
    mg->addWidget(m_alignOther, 2, 5);
    lay->addWidget(margins);

    // logos
    auto* logos = new QGroupBox(tr("Logos"), this);
    auto* lg = new QGridLayout(logos);
    for (int i = 0; i < 8; ++i) {
        const int row = i % 4, col = (i / 4) * 4;
        lg->addWidget(new QLabel(QStringLiteral("%1.").arg(i + 1), logos), row, col);
        m_logos[size_t(i)] = new QLineEdit(m_pp.logos[size_t(i)].path, logos);
        lg->addWidget(m_logos[size_t(i)], row, col + 1);
        auto* browse = new QToolButton(logos);
        browse->setIcon(UiHelpers::icon(QStringLiteral("folder")));
        auto* clear = new QToolButton(logos);
        clear->setIcon(UiHelpers::icon(QStringLiteral("remove")));
        auto* btns = new QHBoxLayout;
        btns->setSpacing(1);
        btns->addWidget(browse);
        btns->addWidget(clear);
        lg->addLayout(btns, row, col + 2);
        QLineEdit* edit = m_logos[size_t(i)];
        connect(browse, &QToolButton::clicked, this, [this, edit] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Logo"), edit->text().isEmpty() ? AppSettings::logosDir() : edit->text(),
                                                           tr("Images (*.png *.jpg *.jpeg *.bmp *.gif)"));
            if (!f.isEmpty())
                edit->setText(f);
        });
        connect(clear, &QToolButton::clicked, edit, &QLineEdit::clear);
    }
    m_appLogo = new QCheckBox(tr("OpenSkiPro2 in the foot of page"), logos);
    m_appLogo->setChecked(m_pp.showAppLogo);
    m_logosFirstOnly = new QCheckBox(tr("Display logos only on 1st page"), logos);
    m_logosFirstOnly->setChecked(m_pp.logosFirstPageOnly);
    auto* positions = new QPushButton(tr("Logo Positions"), logos);
    lg->addWidget(m_appLogo, 4, 0, 1, 2);
    lg->addWidget(m_logosFirstOnly, 4, 2, 1, 3);
    lg->addWidget(positions, 4, 5, 1, 2);
    lg->setColumnStretch(1, 1);
    lg->setColumnStretch(5, 1);
    connect(positions, &QPushButton::clicked, this, [this] {
        PrintingParams pp = m_pp;
        collect(pp);
        LogoPositionsDialog dlg(pp, this);
        if (dlg.exec() == QDialog::Accepted) {
            const auto rects = dlg.rects();
            for (size_t i = 0; i < m_pp.logos.size(); ++i)
                m_pp.logos[i].rect = rects[i];
        }
    });
    lay->addWidget(logos);

    lay->addLayout(UiHelpers::okCancelRow(this));
    resize(860, 0);
}

void PrintingParametersDialog::updateFontButtons()
{
    for (int i = 0; i < 6; ++i) {
        const QFont f = *fontRef(m_pp, i);
        m_fontButtons[size_t(i)]->setText(QStringLiteral("%1 - %2").arg(f.family()).arg(f.pointSize()));
        QFont shown = f;
        shown.setPointSize(qMin(f.pointSize(), 14));
        m_fontButtons[size_t(i)]->setFont(shown);
    }
}

void PrintingParametersDialog::collect(PrintingParams& pp) const
{
    pp.showTitle1 = m_showTitle1->isChecked();
    pp.showTitle2 = m_showTitle2->isChecked();
    pp.autoAdjust = m_autoAdjust->isChecked();
    pp.showAsTable = m_asTable->isChecked();
    pp.grayRows = m_grayRows->isChecked();
    pp.grayLevel = m_grayLevel->value();
    for (int i = 0; i < 4; ++i) {
        pp.marginsFirst[size_t(i)] = m_marginFirst[size_t(i)]->value();
        pp.marginsOther[size_t(i)] = m_marginOther[size_t(i)]->value();
    }
    pp.titleAlignFirst = m_alignFirst->currentIndex();
    pp.titleAlignOther = m_alignOther->currentIndex();
    for (int i = 0; i < 8; ++i)
        pp.logos[size_t(i)].path = m_logos[size_t(i)]->text().trimmed();
    pp.showAppLogo = m_appLogo->isChecked();
    pp.logosFirstPageOnly = m_logosFirstOnly->isChecked();
}

void PrintingParametersDialog::apply(EventParams& p) const
{
    PrintingParams pp = m_pp;
    collect(pp);
    p.printing = pp;
}

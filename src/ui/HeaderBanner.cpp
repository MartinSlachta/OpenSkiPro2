#include "HeaderBanner.h"
#include "UiHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

HeaderBanner::HeaderBanner(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("headerBanner"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(8, 4, 12, 4);

    auto* evIcon = new QToolButton(this);
    evIcon->setIcon(UiHelpers::icon(QStringLiteral("folder")));
    evIcon->setIconSize(QSize(40, 40));
    evIcon->setAutoRaise(true);
    evIcon->setToolTip(tr("Open an Event"));
    connect(evIcon, &QToolButton::clicked, this, &HeaderBanner::openEventRequested);
    lay->addWidget(evIcon);

    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_title = new QLabel(this);
    m_subtitle = new QLabel(this);
    QFont f = m_title->font();
    f.setPointSize(14);
    f.setBold(true);
    m_title->setFont(f);
    QFont s = m_subtitle->font();
    s.setPointSize(10);
    s.setBold(true);
    m_subtitle->setFont(s);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    lay->addLayout(titles);
    lay->addStretch();

    auto* logo = new QLabel(this);
    logo->setPixmap(UiHelpers::icon(QStringLiteral("app")).pixmap(44, 44));
    lay->addWidget(logo);
    auto* brand = new QLabel(this);
    brand->setText(QStringLiteral("<span style='font-size:20pt; font-weight:700; color:#1b5fa8;'>Open</span>"
                                  "<span style='font-size:20pt; font-weight:700; color:#222;'>SkiPro</span>"
                                  "<span style='font-size:20pt; font-weight:700; color:#d93b30;'>2</span><br/>"
                                  "<span style='font-size:8pt; letter-spacing:3px; color:#555;'>ALPINE TIMING</span>"));
    lay->addWidget(brand);
    setFixedHeight(60);
}

void HeaderBanner::setTitles(const QString& title, const QString& subtitle)
{
    m_title->setText(UiHelpers::oneLine(title));
    m_subtitle->setText(UiHelpers::oneLine(subtitle));
}

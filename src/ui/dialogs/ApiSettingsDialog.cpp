#include "ApiSettingsDialog.h"
#include "api/ApiServer.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QLabel>
#include <QNetworkInterface>
#include <QSpinBox>
#include <QVBoxLayout>

ApiSettingsDialog::ApiSettingsDialog(ApiServer* api, QWidget* parent)
    : QDialog(parent)
    , m_api(api)
{
    setWindowTitle(tr("API Server"));
    auto* lay = new QVBoxLayout(this);
    m_enabled = new QCheckBox(tr("Enable the API server"), this);
    m_enabled->setChecked(ApiServer::settingEnabled());
    lay->addWidget(m_enabled);

    auto* g = new QGridLayout;
    g->addWidget(new QLabel(tr("Port :"), this), 0, 0);
    m_port = new QSpinBox(this);
    m_port->setRange(1, 65535);
    m_port->setValue(ApiServer::settingPort());
    g->addWidget(m_port, 0, 1);
    g->setColumnStretch(2, 1);
    g->addWidget(new QLabel(tr("State :"), this), 1, 0);
    m_state = new QLabel(this);
    g->addWidget(m_state, 1, 1, 1, 2);
    g->addWidget(new QLabel(tr("Address :"), this), 2, 0, Qt::AlignTop);
    m_urls = new QLabel(this);
    m_urls->setTextFormat(Qt::RichText);
    m_urls->setTextInteractionFlags(Qt::TextBrowserInteraction); // links open in the browser
    m_urls->setOpenExternalLinks(true);
    g->addWidget(m_urls, 2, 1, 1, 2);
    lay->addLayout(g);

    auto* note = new QLabel(tr("Read-only access to the event and the timing for other applications "
                               "(scoreboards, live results, screens) over REST and WebSocket.\n"
                               "There is no password: use it only in a closed local network."),
                            this);
    note->setWordWrap(true);
    lay->addWidget(note);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(m_port, &QSpinBox::valueChanged, this, &ApiSettingsDialog::updateInfo);
    connect(m_api, &ApiServer::stateChanged, this, &ApiSettingsDialog::updateInfo);
    updateInfo();
    resize(480, 0);
}

void ApiSettingsDialog::updateInfo()
{
    m_state->setText(m_api->stateText());
    QStringList urls{ QStringLiteral("http://localhost:%1/").arg(m_port->value()) };
    for (const QHostAddress& a : QNetworkInterface::allAddresses())
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback() && !a.isLinkLocal())
            urls << QStringLiteral("http://%1:%2/").arg(a.toString()).arg(m_port->value());
    QStringList links;
    for (const QString& u : urls)
        links << QStringLiteral("<a href=\"%1\">%1</a>").arg(u);
    links << QString() << tr("<a href=\"%1docs\">API documentation (Swagger UI)</a>").arg(urls.first());
    m_urls->setText(links.join(QStringLiteral("<br>")));
}

void ApiSettingsDialog::accept()
{
    ApiServer::setSettings(m_enabled->isChecked(), quint16(m_port->value()));
    m_api->applySettings();
    QDialog::accept();
}

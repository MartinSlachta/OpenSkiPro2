#include "TimingDeviceDialog.h"
#include "app/SoundPlayer.h"
#include "core/TimeUtil.h"
#include "timing/Cp540Device.h"
#include "timing/Cp540Simulator.h"
#include "ui/UiHelpers.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSerialPortInfo>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>

TimingDeviceDialog::TimingDeviceDialog(int slot, const TimingDeviceConfig& cfg, QWidget* parent)
    : QDialog(parent)
    , m_slot(slot)
    , m_cfg(cfg)
{
    setWindowTitle(tr("Timing Device %1").arg(slot));
    auto* lay = new QVBoxLayout(this);

    auto* dev = new QGroupBox(tr("Timing Device"), this);
    auto* dg = new QGridLayout(dev);
    m_type = new QComboBox(dev);
    m_type->addItem(tr("None"), int(TimingDeviceConfig::Type::None));
    m_type->addItem(QStringLiteral("TAG HEUER CP 540"), int(TimingDeviceConfig::Type::CP540));
    m_type->addItem(tr("CP 540 simulator (test without timing device)"), int(TimingDeviceConfig::Type::Simulator));
    m_type->setCurrentIndex(qMax(0, m_type->findData(int(cfg.type))));
    dg->addWidget(new QLabel(tr("Type :"), dev), 0, 0);
    dg->addWidget(m_type, 0, 1, 1, 3);
    auto* test = new QPushButton(UiHelpers::icon(QStringLiteral("connect")), tr("Test Timing Device"), dev);
    dg->addWidget(test, 0, 4);

    m_serial = new QRadioButton(tr("Port"), dev);
    m_port = new QComboBox(dev);
    m_port->setEditable(true);
    for (const auto& p : QSerialPortInfo::availablePorts())
        m_port->addItem(p.portName(), p.portName());
    if (!cfg.serialPort.isEmpty() && m_port->findData(cfg.serialPort) < 0)
        m_port->addItem(cfg.serialPort, cfg.serialPort);
    m_port->setCurrentText(cfg.serialPort);
    m_baud = new QComboBox(dev);
    for (int b : { 2400, 9600, 38400, 57600 })
        m_baud->addItem(QString::number(b), b);
    m_baud->setCurrentIndex(qMax(0, m_baud->findData(cfg.baudRate)));
    m_flowAck = new QCheckBox(tr("9600bds Flow Ctrl (ACK)"), dev);
    m_flowAck->setChecked(cfg.flowControlAck);
    dg->addWidget(m_serial, 1, 0);
    dg->addWidget(m_port, 1, 1);
    dg->addWidget(m_baud, 1, 2);
    dg->addWidget(m_flowAck, 1, 3, 1, 2);
    m_tcp = new QRadioButton(tr("IP"), dev);
    m_host = new QLineEdit(cfg.host, dev);
    m_tcpPort = new QSpinBox(dev);
    m_tcpPort->setRange(1, 65535);
    m_tcpPort->setValue(cfg.tcpPort);
    m_tcpPort->setToolTip(tr("CP540 ports: 7000 or 13500-13503 (13500-13503 keep the data when the connection is lost)"));
    dg->addWidget(m_tcp, 2, 0);
    dg->addWidget(m_host, 2, 1);
    dg->addWidget(new QLabel(tr("Port :"), dev), 2, 2, Qt::AlignRight);
    dg->addWidget(m_tcpPort, 2, 3);
    (cfg.link == TimingDeviceConfig::Link::Tcp ? m_tcp : m_serial)->setChecked(true);
    lay->addWidget(dev);

    auto* tabs = new QTabWidget(this);
    // channels
    auto* chan = new QWidget(tabs);
    auto* cg = new QGridLayout(chan);
    m_start = new QSpinBox(chan);
    m_finish = new QSpinBox(chan);
    for (auto* s : { m_start, m_finish })
        s->setRange(0, 99);
    m_start->setValue(cfg.startChannel);
    m_finish->setValue(cfg.finishChannel);
    cg->addWidget(new QLabel(tr("Start :"), chan), 0, 0, Qt::AlignRight);
    cg->addWidget(m_start, 0, 1);
    cg->addWidget(new QLabel(tr("Finish :"), chan), 0, 2, Qt::AlignRight);
    cg->addWidget(m_finish, 0, 3);
    for (int i = 0; i < 3; ++i) {
        m_inter[size_t(i)] = new QSpinBox(chan);
        m_inter[size_t(i)]->setRange(0, 99);
        m_inter[size_t(i)]->setSpecialValueText(QStringLiteral("-"));
        m_inter[size_t(i)]->setValue(cfg.interChannels[size_t(i)]);
        cg->addWidget(new QLabel(tr("Inter %1 :").arg(i + 1), chan), 1 + i, 0, Qt::AlignRight);
        cg->addWidget(m_inter[size_t(i)], 1 + i, 1);
    }
    auto* kb = new QGroupBox(tr("Numbers typed on the CP540 keyboard"), chan);
    auto* kl = new QVBoxLayout(kb);
    kl->addWidget(new QLabel(tr("Use the competitor number sent by the CP540 for these channels\n"
                                "(otherwise OpenSkiPro2 assigns the numbers):"), kb));
    auto* kr = new QHBoxLayout;
    for (int i = 0; i < 4; ++i) {
        m_keyboard[size_t(i)] = new QCheckBox(tr("C%1").arg(i + 1), kb);
        m_keyboard[size_t(i)]->setChecked(cfg.keyboardChannels.contains(i + 1));
        kr->addWidget(m_keyboard[size_t(i)]);
    }
    kr->addStretch();
    kl->addLayout(kr);
    cg->addWidget(kb, 1, 2, 3, 3);
    cg->setRowStretch(4, 1);
    tabs->addTab(chan, tr("Time measurement"));

    // sounds
    auto* snd = new QWidget(tabs);
    auto* sg = new QGridLayout(snd);
    for (int i = 0; i < 4; ++i) {
        m_soundChannel[size_t(i)] = new QSpinBox(snd);
        m_soundChannel[size_t(i)]->setRange(0, 99);
        m_soundChannel[size_t(i)]->setSpecialValueText(QStringLiteral("-"));
        m_soundChannel[size_t(i)]->setValue(cfg.soundChannels[size_t(i)]);
        m_soundFile[size_t(i)] = new QLineEdit(cfg.soundFiles[size_t(i)], snd);
        auto* browse = new QToolButton(snd);
        browse->setIcon(UiHelpers::icon(QStringLiteral("folder")));
        auto* play = new QToolButton(snd);
        play->setText(QStringLiteral("▶"));
        play->setToolTip(tr("Play"));
        QLineEdit* edit = m_soundFile[size_t(i)];
        connect(browse, &QToolButton::clicked, this, [this, edit] {
            const QString f = QFileDialog::getOpenFileName(this, tr("Sound"), edit->text(), tr("Sounds (*.wav)"));
            if (!f.isEmpty())
                edit->setText(f);
        });
        connect(play, &QToolButton::clicked, this, [edit] { SoundPlayer::play(edit->text()); });
        sg->addWidget(new QLabel(tr("Channel :"), snd), i, 0);
        sg->addWidget(m_soundChannel[size_t(i)], i, 1);
        sg->addWidget(new QLabel(tr("File :"), snd), i, 2);
        sg->addWidget(edit, i, 3);
        sg->addWidget(browse, i, 4);
        sg->addWidget(play, i, 5);
    }
    sg->setColumnStretch(3, 1);
    sg->setRowStretch(4, 1);
    tabs->addTab(snd, tr("Sounds"));

    // simulator
    m_simBox = new QGroupBox(tabs);
    auto* simf = new QFormLayout(m_simBox);
    m_simInterval = new QSpinBox(m_simBox);
    m_simInterval->setRange(1, 600);
    m_simInterval->setSuffix(tr(" s"));
    m_simInterval->setValue(cfg.simStartIntervalMs / 1000);
    m_simRunTime = new QSpinBox(m_simBox);
    m_simRunTime->setRange(2, 3600);
    m_simRunTime->setSuffix(tr(" s"));
    m_simRunTime->setValue(cfg.simRunTimeMs / 1000);
    m_simSpread = new QSpinBox(m_simBox);
    m_simSpread->setRange(0, 600);
    m_simSpread->setSuffix(tr(" s"));
    m_simSpread->setValue(cfg.simRunSpreadMs / 1000);
    simf->addRow(tr("Interval between starts (automatic run) :"), m_simInterval);
    simf->addRow(tr("Run time :"), m_simRunTime);
    simf->addRow(tr("Run time spread (+/-) :"), m_simSpread);
    tabs->addTab(m_simBox, tr("Simulator"));
    lay->addWidget(tabs, 1);
    lay->addLayout(UiHelpers::okCancelRow(this));

    connect(m_type, &QComboBox::currentIndexChanged, this, &TimingDeviceDialog::updateEnabled);
    connect(m_serial, &QRadioButton::toggled, this, &TimingDeviceDialog::updateEnabled);
    connect(test, &QPushButton::clicked, this, &TimingDeviceDialog::testDevice);
    updateEnabled();
    resize(720, 520);
}

void TimingDeviceDialog::updateEnabled()
{
    const auto type = TimingDeviceConfig::Type(m_type->currentData().toInt());
    const bool real = type == TimingDeviceConfig::Type::CP540;
    const bool serial = m_serial->isChecked();
    for (QWidget* w : std::initializer_list<QWidget*>{ m_serial, m_tcp })
        w->setEnabled(real);
    m_port->setEnabled(real && serial);
    m_baud->setEnabled(real && serial);
    m_flowAck->setEnabled(real && serial);
    m_host->setEnabled(real && !serial);
    m_tcpPort->setEnabled(real && !serial);
    m_simBox->setEnabled(type == TimingDeviceConfig::Type::Simulator);
}

TimingDeviceConfig TimingDeviceDialog::config() const
{
    TimingDeviceConfig c = m_cfg;
    c.type = TimingDeviceConfig::Type(m_type->currentData().toInt());
    c.link = m_tcp->isChecked() ? TimingDeviceConfig::Link::Tcp : TimingDeviceConfig::Link::Serial;
    c.serialPort = m_port->currentText().trimmed();
    c.baudRate = m_baud->currentData().toInt();
    c.flowControlAck = m_flowAck->isChecked();
    c.host = m_host->text().trimmed();
    c.tcpPort = m_tcpPort->value();
    c.startChannel = m_start->value();
    c.finishChannel = m_finish->value();
    for (int i = 0; i < 3; ++i)
        c.interChannels[size_t(i)] = m_inter[size_t(i)]->value();
    c.keyboardChannels.clear();
    for (int i = 0; i < 4; ++i)
        if (m_keyboard[size_t(i)]->isChecked())
            c.keyboardChannels.insert(i + 1);
    for (int i = 0; i < 4; ++i) {
        c.soundChannels[size_t(i)] = m_soundChannel[size_t(i)]->value();
        c.soundFiles[size_t(i)] = m_soundFile[size_t(i)]->text().trimmed();
    }
    c.simStartIntervalMs = m_simInterval->value() * 1000;
    c.simRunTimeMs = m_simRunTime->value() * 1000;
    c.simRunSpreadMs = m_simSpread->value() * 1000;
    return c;
}

void TimingDeviceDialog::testDevice()
{
    const TimingDeviceConfig c = config();
    if (c.type == TimingDeviceConfig::Type::None)
        return;
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Test Timing Device"));
    auto* lay = new QVBoxLayout(&dlg);
    auto* status = new QLabel(&dlg);
    auto* log = new QPlainTextEdit(&dlg);
    log->setReadOnly(true);
    log->setFont(QFont(QStringLiteral("Courier New"), 10));
    lay->addWidget(status);
    lay->addWidget(new QLabel(tr("Received data (press a channel key on the CP540):"), &dlg));
    lay->addWidget(log, 1);
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), &dlg);
    connect(close, &QPushButton::clicked, &dlg, &QDialog::accept);
    lay->addWidget(close, 0, Qt::AlignRight);

    Cp540Device* device = c.type == TimingDeviceConfig::Type::Simulator ? new Cp540Simulator(m_slot, c, &dlg)
                                                                         : new Cp540Device(m_slot, c, &dlg);
    connect(device, &TimingDevice::rawLine, log, [log](const QString& line) { log->appendPlainText(line); });
    connect(device, &TimingDevice::deviceMessage, log, [log](const QString& m) { log->appendPlainText(QStringLiteral("# ") + m); });
    connect(device, &TimingDevice::statusChanged, status, [status](bool ok, const QString& text) {
        status->setText(QStringLiteral("<b style='color:%1'>%2</b>").arg(ok ? QStringLiteral("green") : QStringLiteral("red"), text));
    });
    connect(device, &TimingDevice::impulse, log, [log](const TimingImpulse& imp) {
        log->appendPlainText(tr("  -> channel %1, seq %2, %3").arg(imp.channel).arg(imp.seq).arg(TimeUtil::formatDayTime(imp.dayTimeUs)));
    });
    QString err;
    if (!device->open(&err))
        status->setText(QStringLiteral("<b style='color:red'>%1</b>").arg(err.toHtmlEscaped()));
    else if (c.type == TimingDeviceConfig::Type::CP540)
        device->sendCommand(Cp540Protocol::requestSerial());
    if (auto* sim = qobject_cast<Cp540Simulator*>(device)) {
        auto* b = new QPushButton(tr("Simulate an impulse on the start channel"), &dlg);
        connect(b, &QPushButton::clicked, sim, [sim, c] { sim->trigger(c.startChannel); });
        lay->insertWidget(1, b);
    }
    dlg.resize(620, 420);
    dlg.exec();
    device->close();
}

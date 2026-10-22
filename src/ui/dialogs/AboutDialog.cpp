#include "AboutDialog.h"
#include "app/AppSettings.h"
#include "ui/UiHelpers.h"

#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About..."));
    auto* lay = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    auto* logo = new QLabel(this);
    logo->setPixmap(UiHelpers::icon(QStringLiteral("app")).pixmap(72, 72));
    top->addWidget(logo, 0, Qt::AlignTop);
    auto* text = new QLabel(this);
    text->setTextFormat(Qt::RichText);
    text->setWordWrap(true);
    text->setText(tr("<h2>OpenSkiPro2 %1</h2>"
                     "<p>Timing and results of alpine ski events.</p>"
                     "<p>Timing device: TAG Heuer CP 540 (THCOM08, RS232 / Ethernet).</p>"
                     "<p>The user interface follows SkiPro 2.27 so that its users feel at home; "
                     "SkiPro events can be imported (Files &gt; Manage Events).</p>"
                     "<p><b>Rules and specifications used</b></p>"
                     "<ul>"
                     "<li>FIS International Competition Rules (ICR) Alpine Skiing, 04.09.2026</li>"
                     "<li>FIS Points Rules Alpine Skiing, edition 2026/27 (01.07.2026)</li>"
                     "<li>FIS Alpine Data Booklet 1.16 with appendix (30.09.2026)</li>"
                     "<li>FIS Alpine Result XML Description 2.12 (10.09.2026)</li>"
                     "</ul>"
                     "<p>Free software under the GNU General Public License version 3.</p>"
                     "<p>Built with Qt %2.</p>"
                     "<p><small>Configuration: %3</small></p>")
                      .arg(QStringLiteral(OSP_VERSION), QString::fromLatin1(qVersion()),
                           QDir::toNativeSeparators(AppSettings::iniPath()).toHtmlEscaped()));
    top->addWidget(text, 1);
    lay->addLayout(top);
    auto* close = UiHelpers::button(QStringLiteral("close"), tr("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    lay->addWidget(close, 0, Qt::AlignRight);
    resize(520, 0);
}

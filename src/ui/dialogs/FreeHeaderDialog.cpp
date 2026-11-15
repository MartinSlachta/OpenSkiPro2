#include "FreeHeaderDialog.h"
#include "SelectEventDialog.h"
#include "app/AppSettings.h"
#include "core/Event.h"
#include "report/FreeHeader.h"
#include "ui/MainWindow.h"
#include "ui/UiHelpers.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QTabWidget>
#include <QTextBlock>
#include <QTextEdit>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// four lines aligned like the text (0 left, 1 centre, 2 right)
QIcon alignIcon(int align)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QPen(QColor(0x33, 0x33, 0x33), 1.6));
    const int widths[] = { 14, 9, 14, 9 };
    for (int i = 0; i < 4; ++i) {
        const int w = widths[i];
        const int x = align == 0 ? 3 : align == 1 ? (20 - w) / 2 : 17 - w;
        p.drawLine(x, 4 + i * 4, x + w, 4 + i * 4);
    }
    return QIcon(pm);
}

// "B", "I", "U" drawn in their own style
QIcon letterIcon(const QString& letter, bool bold, bool italic, bool underline)
{
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    QFont f(QStringLiteral("Times New Roman"), 13);
    f.setBold(bold);
    f.setItalic(italic);
    f.setUnderline(underline);
    p.setFont(f);
    p.setPen(QColor(0x33, 0x33, 0x33));
    p.drawText(QRect(0, 0, 20, 20), Qt::AlignCenter, letter);
    return QIcon(pm);
}

} // namespace

FreeHeaderDialog::FreeHeaderDialog(const EventParams& p, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Header"));
    auto* lay = new QVBoxLayout(this);
    auto* tabs = new QTabWidget(this);
    auto* page = new QWidget(tabs);
    auto* pl = new QVBoxLayout(page);

    m_edit = new QTextEdit(page);
    m_edit->document()->setDefaultFont(FreeHeader::defaultFont());
    m_edit->setAcceptRichText(true); // formatted text pasted from Word keeps its format
    m_edit->setTabChangesFocus(true);
    m_edit->setHtml(p.general.freeHeader);
    pl->addWidget(m_edit, 1);

    auto action = [this](const QIcon& icon, const QString& text, const QKeySequence& key, bool checkable) {
        auto* a = new QAction(icon, text, this);
        a->setShortcut(key);
        a->setCheckable(checkable);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        addAction(a);
        return a;
    };
    m_bold = action(letterIcon(QStringLiteral("B"), true, false, false), tr("Bold"), QKeySequence::Bold, true);
    m_italic = action(letterIcon(QStringLiteral("I"), false, true, false), tr("Italic"), QKeySequence::Italic, true);
    m_underline = action(letterIcon(QStringLiteral("U"), false, false, true), tr("Underline"), QKeySequence::Underline, true);
    m_left = action(alignIcon(0), tr("Align Left"), QKeySequence(), true);
    m_center = action(alignIcon(1), tr("Center"), QKeySequence(), true);
    m_right = action(alignIcon(2), tr("Align Right"), QKeySequence(), true);
    m_font = action(UiHelpers::icon(QStringLiteral("params")), tr("Font..."), QKeySequence(), false);

    connect(m_bold, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat f;
        f.setFontWeight(on ? QFont::Bold : QFont::Normal);
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_italic, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat f;
        f.setFontItalic(on);
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_underline, &QAction::triggered, this, [this](bool on) {
        QTextCharFormat f;
        f.setFontUnderline(on);
        m_edit->mergeCurrentCharFormat(f);
    });
    connect(m_left, &QAction::triggered, this, [this] { m_edit->setAlignment(Qt::AlignLeft | Qt::AlignAbsolute); updateActions(); });
    connect(m_center, &QAction::triggered, this, [this] { m_edit->setAlignment(Qt::AlignHCenter); updateActions(); });
    connect(m_right, &QAction::triggered, this, [this] { m_edit->setAlignment(Qt::AlignRight | Qt::AlignAbsolute); updateActions(); });
    connect(m_font, &QAction::triggered, this, &FreeHeaderDialog::chooseFont);
    connect(m_edit, &QTextEdit::currentCharFormatChanged, this, &FreeHeaderDialog::updateActions);
    connect(m_edit, &QTextEdit::cursorPositionChanged, this, &FreeHeaderDialog::updateActions);

    auto* bar = new QHBoxLayout;
    auto toolButton = [page](QAction* a) {
        auto* b = new QToolButton(page);
        b->setDefaultAction(a);
        b->setAutoRaise(true);
        b->setIconSize(QSize(20, 20));
        return b;
    };
    for (QAction* a : { m_bold, m_italic, m_underline })
        bar->addWidget(toolButton(a));
    bar->addSpacing(12);
    for (QAction* a : { m_left, m_center, m_right })
        bar->addWidget(toolButton(a));
    bar->addSpacing(12);
    bar->addWidget(toolButton(m_font));
    bar->addStretch();
    pl->addLayout(bar);

    // like SkiPro: the formatting also by the right button of the mouse
    m_edit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_edit, &QTextEdit::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu* menu = m_edit->createStandardContextMenu(pos);
        QAction* first = menu->actions().value(0);
        for (QAction* a : { m_font, static_cast<QAction*>(nullptr), m_bold, m_italic, m_underline, static_cast<QAction*>(nullptr),
                            m_left, m_center, m_right, static_cast<QAction*>(nullptr) }) {
            if (a)
                menu->insertAction(first, a);
            else
                menu->insertSeparator(first);
        }
        menu->exec(m_edit->viewport()->mapToGlobal(pos));
        delete menu;
    });

    auto* info = new QLabel(tr("Right click on mouse to change text format. You can also copy/paste from any formatted "
                               "text (Example: Word)"),
                            page);
    info->setWordWrap(true);
    info->setStyleSheet(QStringLiteral("background: #dbeafb; border: 1px solid #b7cde8; padding: 6px;"));
    pl->addWidget(info);
    tabs->addTab(page, tr("Print Heading"));
    lay->addWidget(tabs, 1);

    QPushButton* load = nullptr;
    QPushButton* save = nullptr;
    lay->addLayout(UiHelpers::okCancelRow(this, &load, &save));
    auto* loadMenu = new QMenu(load);
    loadMenu->addAction(tr("Load from a File..."), this, &FreeHeaderDialog::loadFromFile);
    loadMenu->addAction(tr("Load from an Event..."), this, &FreeHeaderDialog::loadFromEvent);
    load->setMenu(loadMenu);
    connect(save, &QPushButton::clicked, this, &FreeHeaderDialog::saveToFile);

    updateActions();
    resize(760, 600);
}

void FreeHeaderDialog::updateActions()
{
    const QTextCharFormat f = m_edit->currentCharFormat();
    m_bold->setChecked(f.fontWeight() >= QFont::Bold);
    m_italic->setChecked(f.fontItalic());
    m_underline->setChecked(f.fontUnderline());
    const Qt::Alignment a = m_edit->alignment() & Qt::AlignHorizontal_Mask & ~Qt::AlignAbsolute;
    m_left->setChecked(a == Qt::AlignLeft || a == 0);
    m_center->setChecked(a == Qt::AlignHCenter);
    m_right->setChecked(a == Qt::AlignRight);
}

void FreeHeaderDialog::chooseFont()
{
    bool ok = false;
    const QFont font = QFontDialog::getFont(&ok, m_edit->currentFont(), this, tr("Font"));
    if (!ok)
        return;
    QTextCharFormat f;
    f.setFont(font);
    m_edit->mergeCurrentCharFormat(f);
}

void FreeHeaderDialog::apply(EventParams& p) const
{
    const QString html = m_edit->toHtml();
    p.general.freeHeader = FreeHeader::isEmpty(html) ? QString() : html;
}

void FreeHeaderDialog::loadFromFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/freeHeader"), AppSettings::exportDir()).toString();
    const QString file = QFileDialog::getOpenFileName(this, tr("Load from a File"), dir,
                                                      tr("Formatted text (*.html *.htm *.rtf);;Text (*.txt);;All files (*)"));
    if (file.isEmpty())
        return;
    AppSettings::setValue(QStringLiteral("folders/freeHeader"), QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, windowTitle(), tr("Cannot read %1: %2").arg(file, f.errorString()));
        return;
    }
    const QByteArray data = f.readAll();
    const QString suffix = QFileInfo(file).suffix().toLower();
    if (suffix == QLatin1String("rtf"))
        m_edit->setHtml(FreeHeader::fromRtf(QString::fromLatin1(data))); // RTF is 7 bit, the rest is escaped
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm"))
        m_edit->setHtml(QString::fromUtf8(data));
    else
        m_edit->setPlainText(QString::fromUtf8(data));
}

void FreeHeaderDialog::loadFromEvent()
{
    SelectEventDialog dlg(SelectEventDialog::Mode::Existing, this);
    dlg.setWindowTitle(tr("Load from an Event"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString html, err;
    {
        Event other;
        if (!other.open(MainWindow::eventPath(dlg.selectedSlot()), &err)) {
            QMessageBox::warning(this, windowTitle(), err);
            return;
        }
        html = other.params().general.freeHeader;
        other.close();
    }
    if (FreeHeader::isEmpty(html)) {
        QMessageBox::information(this, windowTitle(), tr("The selected event has no free header."));
        return;
    }
    m_edit->setHtml(html);
}

void FreeHeaderDialog::saveToFile()
{
    const QString dir = AppSettings::value(QStringLiteral("folders/freeHeader"), AppSettings::exportDir()).toString();
    QDir().mkpath(dir);
    QString file = QFileDialog::getSaveFileName(this, tr("Save to a File"), QDir(dir).absoluteFilePath(QStringLiteral("header.html")),
                                                tr("Formatted text (*.html)"));
    if (file.isEmpty())
        return;
    if (!file.endsWith(QLatin1String(".html"), Qt::CaseInsensitive) && !file.endsWith(QLatin1String(".htm"), Qt::CaseInsensitive))
        file += QStringLiteral(".html");
    AppSettings::setValue(QStringLiteral("folders/freeHeader"), QFileInfo(file).absolutePath());
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(m_edit->toHtml().toUtf8()) < 0)
        QMessageBox::warning(this, windowTitle(), tr("Cannot write %1: %2").arg(file, f.errorString()));
}

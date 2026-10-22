#include "ImpulseCommentDialog.h"
#include "core/RunData.h"
#include "core/TimeUtil.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ImpulseCommentDialog::ImpulseCommentDialog(const TimeRecord& rec, int seq, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Impulse comment"));
    auto* lay = new QVBoxLayout(this);

    // the impulse the comment belongs to
    auto* form = new QFormLayout;
    const QString pointName = rec.point == Point::Start    ? tr("Start")
                            : rec.point == Point::Finish ? tr("Finish")
                                                         : tr("Inter %1").arg(rec.point);
    form->addRow(tr("Point:"), new QLabel(seq > 0 ? QStringLiteral("%1 - %2 %3").arg(pointName, tr("Seq")).arg(seq) : pointName, this));
    form->addRow(tr("Number:"), new QLabel(rec.bib > 0 ? QString::number(rec.bib) : tr("(none)"), this));
    form->addRow(tr("Hour Cell.:"), new QLabel(rec.hasTime() ? TimeUtil::formatDayTime(rec.dayTimeUs) : tr("No time"), this));
    if (rec.device > 0)
        form->addRow(tr("Id:"), new QLabel(rec.deviceSeq > 0 ? QStringLiteral("%1/%2").arg(rec.device).arg(rec.deviceSeq)
                                                             : QString::number(rec.device),
                                           this));
    lay->addLayout(form);

    lay->addWidget(new QLabel(tr("Comment:"), this));
    m_edit = new QPlainTextEdit(rec.note, this);
    m_edit->setTabChangesFocus(true);
    m_edit->setMinimumSize(360, 110);
    lay->addWidget(m_edit);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* clear = buttons->addButton(tr("Delete comment"), QDialogButtonBox::ResetRole);
    clear->setEnabled(!rec.note.isEmpty());
    connect(clear, &QPushButton::clicked, this, [this] {
        m_edit->clear();
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(buttons);

    m_edit->setFocus();
    m_edit->moveCursor(QTextCursor::End);
}

QString ImpulseCommentDialog::comment() const
{
    return m_edit->toPlainText().trimmed();
}

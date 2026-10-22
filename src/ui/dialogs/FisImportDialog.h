#pragma once

#include "core/FisList.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

// "Import FIS list": selection of the FIS points list archive (ALFP<list><season>F.zip)
// or of the folder with its extracted files, preview and import into the FIS list database.
class FisImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit FisImportDialog(QWidget* parent = nullptr);
    int importedCount() const { return m_imported; }
    void setSource(const QString& path);

    void accept() override;

private:
    void browseFile();
    void browseFolder();
    void readSource();

    QLineEdit* m_path;
    QLabel* m_current;
    QLabel* m_info;
    QPushButton* m_ok = nullptr;
    FisList m_list;      // read from the selected source
    FisList m_existing;  // header of the current list
    QString m_readPath;
    int m_imported = 0;
};

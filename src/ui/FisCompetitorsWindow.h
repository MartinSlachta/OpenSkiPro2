#pragma once

#include "core/FisList.h"

#include <QAbstractTableModel>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QWidget>

class Event;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

// Competitors of the FIS list; the ones already in the event (same FIS code) are marked.
class FisCompetitorModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { LastName, FirstName, Code, Gender, Year, Nation, PointsFirst };
    FisCompetitorModel(const FisList* list, QObject* parent = nullptr);
    void setExisting(const QSet<QString>& codes);
    void setBoldDiscipline(int discipline);
    bool isExisting(int row) const;
    int rowCount(const QModelIndex& = QModelIndex()) const override;
    int columnCount(const QModelIndex& = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;

private:
    const FisList* m_list;
    QSet<QString> m_existing;
    int m_boldDiscipline = -1;
};

class FisCompetitorFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setFilter(const QString& name, const QString& code, const QString& nation, int gender);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    QString m_name, m_code, m_nation;
    int m_gender = 0;
};

// "Import Competitors" of the FIS section: separate window with the competitors of the
// FIS list; a double click (or Enter) inserts the selected ones into Manage Competitors.
class FisCompetitorsWindow : public QWidget {
    Q_OBJECT
public:
    FisCompetitorsWindow(Event* ev, const FisList& list, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* e) override;

private:
    void applyFilter();
    void refreshExisting();
    void insertSelected();

    Event* m_ev;
    FisList m_list;
    FisCompetitorModel* m_model;
    FisCompetitorFilter* m_proxy;
    QTableView* m_view;
    QLineEdit* m_name;
    QLineEdit* m_code;
    QLineEdit* m_nation;
    QComboBox* m_gender;
    QLabel* m_count;
};

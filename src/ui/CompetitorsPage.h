#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <QWidget>

class Event;
class QComboBox;
class QLabel;
class QTimer;
class QLineEdit;
class QTableView;

class CompetitorTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit CompetitorTableModel(Event* ev, QObject* parent = nullptr);
    void reload();
    int rowCount(const QModelIndex& = QModelIndex()) const override;
    int columnCount(const QModelIndex& = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    int competitorId(int row) const;
    int fieldAt(int column) const;

private:
    Event* m_ev;
    QList<QPair<int, int>> m_columns;
};

class CompetitorFilterModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setFilter(const QString& bib, const QString& name, int field, const QString& value);

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;
    bool lessThan(const QModelIndex& a, const QModelIndex& b) const override;

private:
    QString m_bib, m_name, m_value;
    int m_field = 0;
};

// "Manage Competitors" page of the main window.
class CompetitorsPage : public QWidget {
    Q_OBJECT
public:
    explicit CompetitorsPage(Event* ev, QWidget* parent = nullptr);

    // opened from the navigation of the main window
    void editFields();
    void editCategories();

private:
    void reload();
    void applyFilter();
    QList<int> selectedIds() const;
    void newCompetitor();
    void modifyCompetitor();
    void deleteCompetitors();
    void importFile();
    void importClipboard();
    void importFromEvent();
    void duplicateSelected();
    void formatField();
    void assignBibs();
    void deleteAll();
    void deleteBibs();
    void saveColumnWidths();

    Event* m_ev;
    CompetitorTableModel* m_model;
    CompetitorFilterModel* m_proxy;
    QTableView* m_view;
    QLabel* m_count;
    QLineEdit* m_searchBib;
    QLineEdit* m_searchName;
    QComboBox* m_searchField;
    QLineEdit* m_searchValue;
    QTimer* m_widthTimer;
    bool m_settingWidths = false; // widths set by the program, not by the operator
};

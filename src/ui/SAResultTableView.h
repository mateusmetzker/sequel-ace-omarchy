//
//  SAResultTableView.h
//  Sequel Ace (Linux port)
//
//  Data grid shared by the content and query views: two-line column headers
//  (name and type), tab-separated copying, context menu with the Sequel Ace
//  copy variants, keyboard handling for NULL/delete/edit-in-sheet.
//
//  Licensed under the MIT license. See LICENSE at the repository root.
//

#pragma once

#include <QHeaderView>
#include <QTableView>

class SAResultModel;

class SAResultHeaderView : public QHeaderView {
    Q_OBJECT
public:
    explicit SAResultHeaderView(QWidget *parent = nullptr);
    void setShowTypes(bool show);
    QSize sizeHint() const override;

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override;

private:
    bool m_showTypes = true;
};

class SAResultTableView : public QTableView {
    Q_OBJECT
public:
    explicit SAResultTableView(QWidget *parent = nullptr);

    void setResultModel(SAResultModel *model);
    SAResultModel *resultModel() const { return m_model; }
    void setEditingEnabled(bool enabled) { m_editingEnabled = enabled; }
    void setShowColumnTypes(bool show);
    void setVerticalGridlines(bool show);

    QList<int> selectedRows() const;
    QString selectionAsText(bool includeHeaders) const;
    void copySelection(bool includeHeaders);
    void autosizeColumns();

Q_SIGNALS:
    void deleteRowsRequested();
    void setNullRequested();
    void editInSheetRequested(const QModelIndex &index);
    void copyAsInsertRequested(bool skipAutoIncrement);
    void addRowRequested();
    void duplicateRowRequested();
    void reloadRequested();
    void columnFilterRequested(const QModelIndex &index);
    void exportRequested();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    SAResultModel *m_model = nullptr;
    SAResultHeaderView *m_header;
    bool m_editingEnabled = false;
};

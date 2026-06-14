#include <QMessageBox>
#include <QFileInfo>

#include "PluginWidget.h"
#include "Misc/PluginDB.h"
#include "Misc/Utility.h"

//------------ modified: removeSelectedPlugins -------------
void PluginWidget::removeSelectedPlugins() {
    QMessageBox msgBox;

    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setWindowFlags(Qt::Window | Qt::WindowStaysOnTopHint);
    msgBox.setWindowTitle(tr("Remove Selected Plugins"));
    msgBox.setText(tr("Are you sure to remove these selected plugins?"));
    QPushButton* yesButton = msgBox.addButton(QMessageBox::Yes);
    QPushButton* noButton = msgBox.addButton(QMessageBox::No);
    msgBox.setDefaultButton(noButton);
    msgBox.exec();
    if (msgBox.clickedButton() != yesButton) {
        return;
    }

    QList<QTableWidgetItem*> itemlist = ui.pluginTable->selectedItems();
    ui.pluginTable->setSortingEnabled(false);

    // now get current settings of the toolbar plugin assignments
    QStringList vals;
    foreach(QComboBox * cb, m_qlcbxs) {
        vals.append(cb->currentText());
    }

    PluginDB* pdb = PluginDB::instance();
    int column_count = ui.pluginTable->columnCount();
    for (int i = 0; i < itemlist.size(); i += column_count) {
        QTableWidgetItem* item = itemlist.at(i);
        int row = ui.pluginTable->row(item);
        QString pluginname = ui.pluginTable->item(row, PluginWidget::NameField)->text();
        ui.pluginTable->removeRow(row);
        pdb->remove_plugin(pluginname);

        // all 10 have the identical lists
        // so remove the same item from each list
        int item_to_remove = ui.comboBox->findText(pluginname);
        if (item_to_remove > -1) {
            foreach(QComboBox * cb, m_qlcbxs) {
                cb->removeItem(item_to_remove);
            }
        }
        int start_i = 0;
        int search_index = vals.indexOf(pluginname, start_i);
        while (search_index > -1) {
            vals[search_index].clear();
            start_i = search_index + 1;
            search_index = vals.indexOf(pluginname, start_i);
        }
    }

    // now put back their current assigned plugins
    int i = 0;
    foreach(QComboBox * cb, m_qlcbxs) {
        int target = cb->findText(vals.at(i));
        cb->setCurrentIndex(target);
        i++;
    }

    ui.pluginTable->resizeColumnsToContents();
    ui.pluginTable->setSortingEnabled(true);
}
//----------------------------------------------------------

//---------------- modified: reInstallPlugin ---------------
void PluginWidget::reInstallPlugin(QString zippath)
{
    QFileInfo zipinfo(zippath);
    QString pluginname = zipinfo.baseName();
    // strip off any versioning present in zip name after first "_" to get internal folder name
    int version_index = pluginname.indexOf("_");
    if (version_index > -1) {
        pluginname.truncate(version_index);
    }

    PluginDB* pdb = PluginDB::instance();
    Utility::removeDir(pdb->pluginsPath() + "/" + pluginname);

    PluginDB::AddResult ar = pdb->add_plugin(zippath, true);
    switch (ar) {
    case PluginDB::AR_XML:
        Utility::DisplayStdWarningDialog(tr("Error: Plugin plugin.xml is invalid or not supported on your operating system."));
        return;
    case PluginDB::AR_SUCCESS:
        break;
    }

    Plugin* p = pdb->get_plugin(pluginname);

    if (p == NULL) {
        return;
    }

    QList<QTableWidgetItem*> items = ui.pluginTable->findItems(pluginname, Qt::MatchExactly);
    foreach(QTableWidgetItem * item, items) {
        if (item->column() != 0)
            continue;
        int row = item->row();
        ui.pluginTable->removeRow(row);
    }

    addNewPluginAssignment(pluginname);

    ui.pluginTable->setSortingEnabled(false);
    int rows = ui.pluginTable->rowCount();
    ui.pluginTable->insertRow(rows);
    setPluginTableRow(p, rows);
    ui.pluginTable->resizeColumnsToContents();
    ui.pluginTable->setSortingEnabled(true);
}
//----------------------------------------------------------

//--------------  modified: addNewPluginAssignment -----------------
void PluginWidget::addNewPluginAssignment(QString pluginname) {
    QStringList vals;
    foreach(QComboBox * cb, m_qlcbxs) {
        vals.append(cb->currentText());
    }
    if (vals.indexOf(pluginname) == -1) {
        int insert_index = vals.indexOf(QString());
        if (insert_index > -1) {
            m_qlcbxs[insert_index]->setCurrentText(pluginname);
        }
    }
}
//-----------------------------------------------------------------

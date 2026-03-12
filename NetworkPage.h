#ifndef NETWORKPAGE_H
#define NETWORKPAGE_H

#include "BasePage.h"

class ElaTableView;
class QStandardItemModel;
class ElaComboBox;
class ElaLineEdit;
class ElaPushButton;

class NetworkPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit NetworkPage(QWidget* parent = nullptr);
    ~NetworkPage() override;

private:
    void refreshSwitchTable();
    void onCreateSwitch();
    void onDeleteSwitch();
    QString selectedSwitchName() const;

    ElaTableView* _switchTable{nullptr};
    QStandardItemModel* _switchModel{nullptr};

    ElaLineEdit* _newSwitchName{nullptr};
    ElaComboBox* _switchTypeCombo{nullptr};
    ElaComboBox* _adapterCombo{nullptr};
    bool _adaptersReady{false};
};

#endif

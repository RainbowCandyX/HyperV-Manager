#ifndef DDAPAGE_H
#define DDAPAGE_H

#include "BasePage.h"

class ElaTableView;
class QStandardItemModel;
class ElaComboBox;
class ElaPushButton;
class ElaText;

class DDAPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit DDAPage(QWidget* parent = nullptr);
    ~DDAPage() override;

private:
    void refreshDevices();
    void onDismountDevice();
    void onAssignToVM();
    void onRemoveFromVM();
    void onMountToHost();
    int selectedDeviceRow() const;

    ElaTableView* _deviceTable{nullptr};
    QStandardItemModel* _deviceModel{nullptr};
    ElaComboBox* _vmCombo{nullptr};
    ElaPushButton* _dismountBtn{nullptr};
    ElaPushButton* _assignBtn{nullptr};
    ElaPushButton* _removeBtn{nullptr};
    ElaPushButton* _mountBtn{nullptr};
    ElaText* _statusText{nullptr};
};

#endif

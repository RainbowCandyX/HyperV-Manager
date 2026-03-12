#ifndef USBPAGE_H
#define USBPAGE_H

#include "BasePage.h"

class ElaTableView;
class QStandardItemModel;
class ElaText;

class USBPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit USBPage(QWidget* parent = nullptr);
    ~USBPage() override;

private:
    void refreshDevices();
    void onDevicesRefreshed();

    ElaTableView* _table{nullptr};
    QStandardItemModel* _model{nullptr};
    ElaText* _statusText{nullptr};
};

#endif

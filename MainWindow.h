#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "ElaWindow.h"

class HomePage;
class VirtualMachinePage;
class CreateVMPage;
class GpuSettingsPage;
class NetworkPage;
class DDAPage;
class USBPage;
class SettingPage;
class ElaContentDialog;

class MainWindow : public ElaWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void initWindow();
    void initContent();

    HomePage* _homePage{nullptr};
    VirtualMachinePage* _vmPage{nullptr};
    CreateVMPage* _createVMPage{nullptr};
    GpuSettingsPage* _gpuSettingsPage{nullptr};
    NetworkPage* _networkPage{nullptr};
    DDAPage* _ddaPage{nullptr};
    USBPage* _usbPage{nullptr};
    SettingPage* _settingPage{nullptr};
    QString _settingKey;
};

#endif

#include "MainWindow.h"
#include "HyperVManager.h"

#include "HomePage.h"
#include "VirtualMachinePage.h"
#include "CreateVMPage.h"
#include "GpuSettingsPage.h"
#include "NetworkPage.h"
#include "DDAPage.h"
#include "USBPage.h"
#include "SettingPage.h"

#include "ElaMessageBar.h"

#include <QApplication>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : ElaWindow(parent)
{
    initWindow();
    initContent();

    connect(HyperVManager::getInstance(), &HyperVManager::errorOccurred, this, [=](const QString& message)
    {
        ElaMessageBar::error(ElaMessageBarType::TopRight, "错误", message, 5000);
    });

    HyperVManager::getInstance()->refreshVMList();
}

MainWindow::~MainWindow()
{
}

void MainWindow::initWindow()
{
    setWindowTitle("Hyper-V Manager");
    setWindowIcon(QIcon(":/Resource/Image/HyperV.png"));
    resize(1100, 700);

    setUserInfoCardVisible(false);
    setNavigationBarWidth(200);
    setThemeChangeTime(0);
}

void MainWindow::initContent()
{
    _homePage = new HomePage(this);
    _vmPage = new VirtualMachinePage(this);
    _createVMPage = new CreateVMPage(this);
    _gpuSettingsPage = new GpuSettingsPage(this);
    _networkPage = new NetworkPage(this);
    _ddaPage = new DDAPage(this);
    _usbPage = new USBPage(this);
    _settingPage = new SettingPage(this);

    addPageNode("主页", _homePage, ElaIconType::House);

    QString vmKey;
    addExpanderNode("虚拟机", vmKey, ElaIconType::Server);
    addPageNode("虚拟机列表", _vmPage, vmKey, ElaIconType::List);
    addPageNode("新建虚拟机", _createVMPage, vmKey, ElaIconType::CirclePlus);
    addPageNode("GPU-P 设置", _gpuSettingsPage, vmKey, ElaIconType::Microchip);
    expandNavigationNode(vmKey);

    QString hwKey;
    addExpanderNode("硬件管理", hwKey, ElaIconType::Screwdriver);
    addPageNode("网络管理", _networkPage, hwKey, ElaIconType::Ethernet);
    addPageNode("PCIe 直通", _ddaPage, hwKey, ElaIconType::Plug);
    addPageNode("USB 设备", _usbPage, hwKey, ElaIconType::UsbDrive);

    addFooterNode("设置", _settingPage, _settingKey, 0, ElaIconType::GearComplex);

    QSettings settings(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    int navMode = settings.value("ui/navMode", 0).toInt();
    setNavigationBarDisplayMode(static_cast<ElaNavigationType::NavigationDisplayMode>(navMode));
}

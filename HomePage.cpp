#include "HomePage.h"
#include "HyperVManager.h"

#include <QHBoxLayout>
#include <QPointer>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QVBoxLayout>

#include <windows.h>

#include "ElaImageCard.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaMessageBar.h"
#include "ElaComboBox.h"
#include "ElaScrollPageArea.h"
#include "ElaToggleSwitch.h"

static QWidget* createInfoRow(const QString& label, const QString& value, QWidget *parent)
{
    ElaScrollPageArea *area = new ElaScrollPageArea(parent);
    QHBoxLayout *layout = new QHBoxLayout(area);
    ElaText *labelText = new ElaText(label, parent);
    labelText->setWordWrap(false);
    labelText->setTextPixelSize(15);
    ElaText *valueText = new ElaText(value, parent);
    valueText->setWordWrap(false);
    valueText->setTextPixelSize(15);
    layout->addWidget(labelText);
    layout->addStretch();
    layout->addWidget(valueText);
    return area;
}

static QWidget* createInfoRowDynamic(const QString& label, ElaText *& valueText, QWidget *parent)
{
    ElaScrollPageArea *area = new ElaScrollPageArea(parent);
    QHBoxLayout *layout = new QHBoxLayout(area);
    ElaText *labelText = new ElaText(label, parent);
    labelText->setWordWrap(false);
    labelText->setTextPixelSize(15);
    valueText = new ElaText("...", parent);
    valueText->setWordWrap(false);
    valueText->setTextPixelSize(15);
    layout->addWidget(labelText);
    layout->addStretch();
    layout->addWidget(valueText);
    return area;
}

static QWidget* createStatCard(const QString& title, ElaText *& valueText, QWidget *parent)
{
    ElaScrollPageArea *card = new ElaScrollPageArea(parent);
    card->setFixedHeight(100);

    QVBoxLayout *layout = new QVBoxLayout(card);
    layout->setAlignment(Qt::AlignCenter);

    ElaText *titleLabel = new ElaText(title, parent);
    titleLabel->setTextPixelSize(14);
    titleLabel->setAlignment(Qt::AlignCenter);

    valueText = new ElaText("0", parent);
    valueText->setTextPixelSize(36);
    valueText->setAlignment(Qt::AlignCenter);

    layout->addWidget(titleLabel);
    layout->addWidget(valueText);
    return card;
}

HomePage::HomePage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("主页");
    setTitleVisible(false);
    setContentsMargins(2, 2, 0, 0);

    ElaText *titleText = new ElaText("Hyper-V 管理器", this);
    titleText->setTextPixelSize(32);

    ElaText *subText = new ElaText("虚拟机管理控制面板", this);
    subText->setTextPixelSize(16);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setContentsMargins(30, 20, 0, 0);
    titleLayout->addWidget(titleText);
    titleLayout->addWidget(subText);

    QHBoxLayout *statsLayout = new QHBoxLayout();
    statsLayout->setContentsMargins(30, 0, 30, 0);
    statsLayout->setSpacing(15);

    statsLayout->addWidget(createStatCard("虚拟机总数", _totalCountText, this));
    statsLayout->addWidget(createStatCard("运行中", _runningCountText, this));
    statsLayout->addWidget(createStatCard("已关闭", _offCountText, this));
    statsLayout->addWidget(createStatCard("已暂停", _pausedCountText, this));

    ElaText *sysInfoTitle = new ElaText("系统信息", this);
    sysInfoTitle->setTextPixelSize(18);
    QHBoxLayout *sysInfoTitleLayout = new QHBoxLayout();
    sysInfoTitleLayout->setContentsMargins(30, 0, 0, 0);
    sysInfoTitleLayout->addWidget(sysInfoTitle);

    QString cpuName;
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        WCHAR buf[256];
        DWORD bufSize = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &bufSize) == ERROR_SUCCESS)
        {
            cpuName = QString::fromWCharArray(buf).trimmed();
        }
        RegCloseKey(hKey);
    }
    int cpuCores = QThread::idealThreadCount();

    MEMORYSTATUSEX memStatus;
    memStatus.dwLength = sizeof(memStatus);
    GlobalMemoryStatusEx(&memStatus);
    double totalGB = memStatus.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double availGB = memStatus.ullAvailPhys / (1024.0 * 1024.0 * 1024.0);
    QString memoryInfo = QString("%1 GB (可用 %2 GB)")
                         .arg(totalGB, 0, 'f', 1)
                         .arg(availGB, 0, 'f', 1);

    QStringList diskParts;
    for (const QStorageInfo& storage : QStorageInfo::mountedVolumes())
    {
        if (storage.isValid() && storage.isReady() && !storage.isReadOnly())
        {
            double totalGB = storage.bytesTotal() / (1024.0 * 1024.0 * 1024.0);
            double freeGB = storage.bytesFree() / (1024.0 * 1024.0 * 1024.0);
            if (totalGB > 0)
            {
                QString drive = storage.rootPath().remove('/');
                diskParts << QString("%1 %2/%3 GB")
                             .arg(drive)
                             .arg(freeGB, 0, 'f', 1)
                             .arg(totalGB, 0, 'f', 1);
            }
        }
    }
    QString diskInfo = diskParts.join("  |  ");

    QString osVersion = QSysInfo::prettyProductName();

    QVBoxLayout *sysInfoLayout = new QVBoxLayout();
    sysInfoLayout->setContentsMargins(30, 0, 30, 0);
    sysInfoLayout->setSpacing(4);
    sysInfoLayout->addWidget(createInfoRow("操作系统", osVersion, this));
    sysInfoLayout->addWidget(createInfoRow("处理器", QString("%1 (%2 核心)").arg(cpuName).arg(cpuCores), this));
    sysInfoLayout->addWidget(createInfoRow("物理内存", memoryInfo, this));
    sysInfoLayout->addWidget(createInfoRow("磁盘", diskInfo, this));

    ElaText *hvInfoTitle = new ElaText("Hyper-V 环境", this);
    hvInfoTitle->setTextPixelSize(18);
    QHBoxLayout *hvInfoTitleLayout = new QHBoxLayout();
    hvInfoTitleLayout->setContentsMargins(30, 0, 0, 0);
    hvInfoTitleLayout->addWidget(hvInfoTitle);

    QVBoxLayout *hvInfoLayout = new QVBoxLayout();
    hvInfoLayout->setContentsMargins(30, 0, 30, 0);
    hvInfoLayout->setSpacing(4);
    hvInfoLayout->addWidget(createInfoRowDynamic("虚拟机监控程序", _hypervisorText, this));
    hvInfoLayout->addWidget(createInfoRowDynamic("IOMMU (VT-d/AMD-Vi)", _iommuText, this));
    {
        ElaScrollPageArea *schedulerArea = new ElaScrollPageArea(this);
        QHBoxLayout *schedulerLayout = new QHBoxLayout(schedulerArea);
        ElaText *schedulerLabel = new ElaText("调度器类型", this);
        schedulerLabel->setWordWrap(false);
        schedulerLabel->setTextPixelSize(15);
        _schedulerCombo = new ElaComboBox(this);
        _schedulerCombo->addItems({"Classic", "Core", "Root"});
        _schedulerCombo->setEnabled(false);
        _schedulerCombo->setFixedWidth(120);
        schedulerLayout->addWidget(schedulerLabel);
        schedulerLayout->addStretch();
        schedulerLayout->addWidget(_schedulerCombo);
        hvInfoLayout->addWidget(schedulerArea);

        connect(_schedulerCombo, &ElaComboBox::currentTextChanged, this, [this](const QString& text)
        {
            _schedulerCombo->setEnabled(false);
            QPointer<HomePage> guard(this);
            HyperVManager::getInstance()->setSchedulerTypeAsync(text, [guard, text](bool success, const QString& error)
            {
                if (!guard) return;
                guard->_schedulerCombo->setEnabled(true);
                if (success)
                {
                    ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功",
                                           "调度器类型已更改为 " + text + "，重启计算机后生效", 3000);
                }
                else
                {
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                         "调度器设置失败: " + error, 5000);
                    guard->loadHostInfo();
                }
            });
        });
    }
    {
        ElaScrollPageArea *numaArea = new ElaScrollPageArea(this);
        QHBoxLayout *numaLayout = new QHBoxLayout(numaArea);
        ElaText *numaLabel = new ElaText("允许 NUMA 跨越", this);
        numaLabel->setWordWrap(false);
        numaLabel->setTextPixelSize(15);
        _numaSwitch = new ElaToggleSwitch(this);
        _numaSwitch->setEnabled(false);
        numaLayout->addWidget(numaLabel);
        numaLayout->addStretch();
        numaLayout->addWidget(_numaSwitch);
        hvInfoLayout->addWidget(numaArea);

        connect(_numaSwitch, &ElaToggleSwitch::toggled, this, [this](bool checked)
        {
            _numaSwitch->setEnabled(false);
            QPointer<HomePage> guard(this);
            HyperVManager::getInstance()->setNumaSpanningAsync(checked, [guard, checked](bool success, const QString& error)
            {
                if (!guard) return;
                guard->_numaSwitch->setEnabled(true);
                if (success)
                {
                    ElaMessageBar::success(ElaMessageBarType::TopRight, "成功",
                                           "NUMA 设置已更新，重启计算机后生效", 3000);
                }
                else
                {
                    guard->_numaSwitch->blockSignals(true);
                    guard->_numaSwitch->setIsToggled(!checked);
                    guard->_numaSwitch->blockSignals(false);
                    ElaMessageBar::error(ElaMessageBarType::TopRight, "失败",
                                         "NUMA 设置失败: " + error, 5000);
                }
            });
        });
    }
    {
        ElaScrollPageArea *gpuStrategyArea = new ElaScrollPageArea(this);
        QHBoxLayout *gpuStrategyLayout = new QHBoxLayout(gpuStrategyArea);
        QVBoxLayout *gpuStrategyLabelLayout = new QVBoxLayout();
        gpuStrategyLabelLayout->setSpacing(2);
        ElaText *gpuStrategyLabel = new ElaText("解锁消费级硬件限制", this);
        gpuStrategyLabel->setWordWrap(false);
        gpuStrategyLabel->setTextPixelSize(15);
        ElaText *gpuStrategyDesc = new ElaText("修复因硬件安全检查导致的 DDA 直通问题", this);
        gpuStrategyDesc->setWordWrap(false);
        gpuStrategyDesc->setTextPixelSize(12);
        gpuStrategyLabelLayout->addWidget(gpuStrategyLabel);
        gpuStrategyLabelLayout->addWidget(gpuStrategyDesc);
        _gpuStrategySwitch = new ElaToggleSwitch(this);
        _gpuStrategySwitch->setEnabled(false);
        gpuStrategyLayout->addLayout(gpuStrategyLabelLayout);
        gpuStrategyLayout->addStretch();
        gpuStrategyLayout->addWidget(_gpuStrategySwitch);
        hvInfoLayout->addWidget(gpuStrategyArea);

        connect(_gpuStrategySwitch, &ElaToggleSwitch::toggled, this, [this](bool checked)
        {
            _gpuStrategySwitch->setEnabled(false);
            QPointer<HomePage> guard(this);
            HyperVManager::getInstance()->setGpuStrategyAsync(checked, [guard, checked](bool success, const QString& error)
            {
                if (!guard) return;
                guard->_gpuStrategySwitch->setEnabled(true);
                if (success)
                {
                    ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功",
                                           checked ? "已解锁消费级硬件限制" : "已恢复硬件限制", 3000);
                }
                else
                {
                    guard->_gpuStrategySwitch->blockSignals(true);
                    guard->_gpuStrategySwitch->setIsToggled(!checked);
                    guard->_gpuStrategySwitch->blockSignals(false);
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                         "设置失败: " + error, 5000);
                }
            });
        });
    }
    {
        ElaScrollPageArea *serverArea = new ElaScrollPageArea(this);
        QHBoxLayout *serverLayout = new QHBoxLayout(serverArea);
        QVBoxLayout *serverLabelLayout = new QVBoxLayout();
        serverLabelLayout->setSpacing(2);
        ElaText *serverLabel = new ElaText("切换系统为服务器版本", this);
        serverLabel->setWordWrap(false);
        serverLabel->setTextPixelSize(15);
        _serverDescText = new ElaText("检测中...", this);
        _serverDescText->setWordWrap(false);
        _serverDescText->setTextPixelSize(12);
        serverLabelLayout->addWidget(serverLabel);
        serverLabelLayout->addWidget(_serverDescText);
        _serverSwitch = new ElaToggleSwitch(this);
        _serverSwitch->setEnabled(false);
        serverLayout->addLayout(serverLabelLayout);
        serverLayout->addStretch();
        serverLayout->addWidget(_serverSwitch);
        hvInfoLayout->addWidget(serverArea);

        connect(_serverSwitch, &ElaToggleSwitch::toggled, this, [this](bool checked)
        {
            _serverSwitch->setEnabled(false);
            QPointer<HomePage> guard(this);
            HyperVManager::getInstance()->switchSystemVersionAsync(checked, [guard, checked](bool success, const QString& error)
            {
                if (!guard) return;
                if (success)
                {
                    guard->_serverDescText->setText(QString("当前: %1").arg(checked ? "服务器版" : "工作站版"));
                    ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功",
                                           "系统版本已切换，请重启计算机以生效", 5000);
                }
                else
                {
                    guard->_serverSwitch->blockSignals(true);
                    guard->_serverSwitch->setIsToggled(!checked);
                    guard->_serverSwitch->blockSignals(false);
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败", error, 5000);
                }
                guard->_serverSwitch->setEnabled(true);
            });
        });
    }
    hvInfoLayout->addWidget(createInfoRowDynamic("默认 VM 路径", _vmPathText, this));

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("主页");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setSpacing(0);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addLayout(titleLayout);
    centerLayout->addSpacing(30);
    centerLayout->addLayout(statsLayout);
    centerLayout->addSpacing(20);
    centerLayout->addLayout(sysInfoTitleLayout);
    centerLayout->addSpacing(5);
    centerLayout->addLayout(sysInfoLayout);
    centerLayout->addSpacing(20);
    centerLayout->addLayout(hvInfoTitleLayout);
    centerLayout->addSpacing(5);
    centerLayout->addLayout(hvInfoLayout);
    centerLayout->addSpacing(20);
    centerLayout->addStretch();
    addCentralWidget(centralWidget);

    connect(HyperVManager::getInstance(), &HyperVManager::vmListRefreshed, this, &HomePage::updateVMStats);
    updateVMStats();
    loadHostInfo();
}

HomePage::~HomePage()
{
}

void HomePage::updateVMStats()
{
    auto vmList = HyperVManager::getInstance()->getVMList();
    int running = 0, off = 0, paused = 0;
    for (const auto& vm : vmList)
    {
        if (vm.state == "Running") running++;
        else if (vm.state == "Off") off++;
        else if (vm.state == "Paused") paused++;
    }
    _totalCountText->setText(QString::number(vmList.size()));
    _runningCountText->setText(QString::number(running));
    _offCountText->setText(QString::number(off));
    _pausedCountText->setText(QString::number(paused));
}

void HomePage::loadHostInfo()
{
    QPointer<HomePage> guard(this);
    HyperVManager::getInstance()->getHostInfoAsync([guard](const HyperVHostInfo& info, const QString& error)
    {
        if (!guard) return;

        if (!error.isEmpty())
        {
            guard->_hypervisorText->setText("读取失败");
            guard->_iommuText->setText("读取失败");
            guard->_vmPathText->setText("读取失败");
            return;
        }

        guard->_hypervisorText->setText(info.hypervisorPresent ? "已启用" : "未启用");
        guard->_iommuText->setText(info.iommuAvailable ? "可用" : "不可用 / 未检测到");

        guard->_schedulerCombo->blockSignals(true);
        int idx = guard->_schedulerCombo->findText(info.schedulerType);
        if (idx >= 0)
            guard->_schedulerCombo->setCurrentIndex(idx);
        guard->_schedulerCombo->setEnabled(true);
        guard->_schedulerCombo->blockSignals(false);
        guard->_numaSwitch->blockSignals(true);
        guard->_numaSwitch->setIsToggled(info.numaSpanningEnabled);
        guard->_numaSwitch->setEnabled(true);
        guard->_numaSwitch->blockSignals(false);

        guard->_vmPathText->setText(info.vmHost.isEmpty() ? "未设置" : info.vmHost);
    });

    HyperVManager::getInstance()->checkGpuStrategyAsync([guard](bool enabled, const QString& error)
    {
        if (!guard) return;
        guard->_gpuStrategySwitch->blockSignals(true);
        guard->_gpuStrategySwitch->setIsToggled(enabled);
        guard->_gpuStrategySwitch->setEnabled(true);
        guard->_gpuStrategySwitch->blockSignals(false);
    });

    HyperVManager::getInstance()->checkIsServerSystemAsync([guard](bool isServer, bool switchEnabled, const QString& error)
    {
        if (!guard) return;
        guard->_serverSwitch->blockSignals(true);
        guard->_serverSwitch->setIsToggled(isServer);
        guard->_serverSwitch->setEnabled(switchEnabled);
        guard->_serverSwitch->blockSignals(false);
        guard->_serverDescText->setText(QString("当前: %1").arg(isServer ? "服务器版" : "工作站版"));
    });
}
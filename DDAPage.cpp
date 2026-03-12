#include "DDAPage.h"
#include "HyperVManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToolButton.h"

DDAPage::DDAPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("PCIe 直通");

    ElaScrollPageArea *toolArea = new ElaScrollPageArea(this);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolArea);

    ElaToolButton *refreshBtn = new ElaToolButton(this);
    refreshBtn->setElaIcon(ElaIconType::ArrowRotateRight);
    refreshBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    refreshBtn->setText("刷新");
    connect(refreshBtn, &ElaToolButton::clicked, this, &DDAPage::refreshDevices);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addStretch();

    _deviceModel = new QStandardItemModel(this);
    _deviceModel->setHorizontalHeaderLabels({"设备名称", "类型", "状态", "分配VM", "实例ID"});

    _deviceTable = new ElaTableView(this);
    _deviceTable->setModel(_deviceModel);
    _deviceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _deviceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _deviceTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _deviceTable->setAlternatingRowColors(true);
    _deviceTable->horizontalHeader()->setStretchLastSection(true);
    _deviceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _deviceTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    _deviceTable->verticalHeader()->setVisible(false);
    _deviceTable->setMinimumHeight(350);

    _vmCombo = new ElaComboBox(this);
    _vmCombo->setFixedWidth(200);

    _dismountBtn = new ElaPushButton("从宿主机卸载", this);
    _dismountBtn->setElaIcon(ElaIconType::Eject, 14);
    _dismountBtn->setFixedSize(140, 36);
    connect(_dismountBtn, &ElaPushButton::clicked, this, &DDAPage::onDismountDevice);

    _assignBtn = new ElaPushButton("分配给 VM", this);
    _assignBtn->setElaIcon(ElaIconType::ArrowRight, 14);
    _assignBtn->setFixedSize(130, 36);
    connect(_assignBtn, &ElaPushButton::clicked, this, &DDAPage::onAssignToVM);

    _removeBtn = new ElaPushButton("从 VM 移除", this);
    _removeBtn->setElaIcon(ElaIconType::ArrowLeft, 14);
    _removeBtn->setFixedSize(130, 36);
    connect(_removeBtn, &ElaPushButton::clicked, this, &DDAPage::onRemoveFromVM);

    _mountBtn = new ElaPushButton("挂载回宿主机", this);
    _mountBtn->setElaIcon(ElaIconType::ArrowUpFromLine, 14);
    _mountBtn->setFixedSize(140, 36);
    connect(_mountBtn, &ElaPushButton::clicked, this, &DDAPage::onMountToHost);

    _statusText = new ElaText("提示：DDA（离散设备分配）允许将 PCIe 设备直接分配给虚拟机。"
                              "需要 IOMMU (VT-d/AMD-Vi) 支持。", this);
    _statusText->setWordWrap(true);
    _statusText->setTextPixelSize(13);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("PCIe 直通 (DDA)");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(toolArea);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(_deviceTable);
    centerLayout->addSpacing(10);

    ElaScrollPageArea *vmArea = new ElaScrollPageArea(this);
    QHBoxLayout *vmLayout = new QHBoxLayout(vmArea);
    ElaText *vmLabel = new ElaText("目标虚拟机", this);
    vmLabel->setTextPixelSize(15);
    vmLayout->addWidget(vmLabel);
    vmLayout->addStretch();
    vmLayout->addWidget(_vmCombo);
    centerLayout->addWidget(vmArea);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(_dismountBtn);
    btnLayout->addWidget(_assignBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(_removeBtn);
    btnLayout->addWidget(_mountBtn);
    centerLayout->addSpacing(10);
    centerLayout->addLayout(btnLayout);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(_statusText);
    centerLayout->addStretch();

    addCentralWidget(centralWidget, true, true, 0);

    connect(HyperVManager::getInstance(), &HyperVManager::vmListRefreshed, this, [this]()
    {
        const QString current = _vmCombo->currentText();
        _vmCombo->clear();
        for (const auto& vm : HyperVManager::getInstance()->getVMList())
        {
            _vmCombo->addItem(vm.name);
        }
        int idx = _vmCombo->findText(current);
        if (idx >= 0) _vmCombo->setCurrentIndex(idx);
    });

    connect(HyperVManager::getInstance(), &HyperVManager::operationFinished, this,
            [this](const QString&, const QString& operation, bool success, const QString& message)
            {
                if (operation.contains("设备"))
                {
                    if (success)
                    {
                        ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功", operation + "成功", 2000);
                        refreshDevices();
                    }
                    else
                    {
                        ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                             operation + "失败: " + message, 5000);
                    }
                }
            });

    for (const auto& vm : HyperVManager::getInstance()->getVMList())
    {
        _vmCombo->addItem(vm.name);
    }
    refreshDevices();
}

DDAPage::~DDAPage()
{
}

void DDAPage::refreshDevices()
{
    _statusText->setText("正在扫描 PCI 设备...");
    QPointer<DDAPage> guard(this);
    HyperVManager::getInstance()->getDDADevicesAsync([guard](const QList<DDADeviceInfo>& devices, const QString& error)
    {
        if (!guard) return;
        guard->_deviceModel->removeRows(0, guard->_deviceModel->rowCount());

        if (!error.isEmpty())
        {
            guard->_statusText->setText("扫描失败：" + error);
            return;
        }

        for (const auto& dev : devices)
        {
            auto makeItem = [](const QString& text)
            {
                QStandardItem *item = new QStandardItem(text);
                item->setTextAlignment(Qt::AlignCenter);
                return item;
            };

            QString classDisplay = dev.deviceClass;
            if (classDisplay == "Display") classDisplay = "显卡";
            else if (classDisplay == "Net") classDisplay = "网卡";
            else if (classDisplay == "USB") classDisplay = "USB 控制器";
            else if (classDisplay == "AudioEndpoint" || classDisplay == "Media") classDisplay = "音频";
            else if (classDisplay == "SCSIAdapter") classDisplay = "存储控制器";

            QString statusDisplay;
            if (!dev.assignedVm.isEmpty())
            {
                statusDisplay = "已分配";
            }
            else if (dev.status == "OK")
            {
                statusDisplay = "宿主机使用中";
            }
            else
            {
                statusDisplay = "已卸载";
            }

            QList<QStandardItem*> row;
            row << makeItem(dev.friendlyName);
            row << makeItem(classDisplay);
            row << makeItem(statusDisplay);
            row << makeItem(dev.assignedVm.isEmpty() ? "-" : dev.assignedVm);
            row << makeItem(dev.instanceId);

            row[0]->setData(dev.locationPath, Qt::UserRole);
            row[0]->setData(dev.instanceId, Qt::UserRole + 1);

            guard->_deviceModel->appendRow(row);
        }
        guard->_statusText->setText(QString("共扫描到 %1 个 PCI 设备。"
                "DDA 需要 IOMMU 支持，请确保已在 BIOS 中启用 VT-d/AMD-Vi。")
            .arg(devices.size()));
    });
}

int DDAPage::selectedDeviceRow() const
{
    QModelIndex idx = _deviceTable->currentIndex();
    if (!idx.isValid()) return -1;
    return idx.row();
}

void DDAPage::onDismountDevice()
{
    int row = selectedDeviceRow();
    if (row < 0)
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个设备", 2500);
        return;
    }

    QString instanceId = _deviceModel->item(row, 0)->data(Qt::UserRole + 1).toString();
    QString locationPath = _deviceModel->item(row, 0)->data(Qt::UserRole).toString();

    if (locationPath.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "无法获取设备位置路径", 2500);
        return;
    }

    HyperVManager::getInstance()->dismountHostDevice(instanceId, locationPath);
    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在卸载设备...", 2000);
}

void DDAPage::onAssignToVM()
{
    int row = selectedDeviceRow();
    if (row < 0)
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个设备", 2500);
        return;
    }

    const QString vmName = _vmCombo->currentText();
    if (vmName.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请选择目标虚拟机", 2500);
        return;
    }

    QString locationPath = _deviceModel->item(row, 0)->data(Qt::UserRole).toString();
    if (locationPath.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "无法获取设备位置路径", 2500);
        return;
    }

    HyperVManager::getInstance()->assignDeviceToVM(vmName, locationPath);
    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息",
                               QString("正在分配设备到 %1...").arg(vmName), 2000);
}

void DDAPage::onRemoveFromVM()
{
    int row = selectedDeviceRow();
    if (row < 0)
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个设备", 2500);
        return;
    }

    const QString assignedVm = _deviceModel->item(row, 3)->text();
    if (assignedVm.isEmpty() || assignedVm == "-")
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "该设备未分配给虚拟机", 2500);
        return;
    }

    QString locationPath = _deviceModel->item(row, 0)->data(Qt::UserRole).toString();
    HyperVManager::getInstance()->removeDeviceFromVM(assignedVm, locationPath);
    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息",
                               QString("正在从 %1 移除设备...").arg(assignedVm), 2000);
}

void DDAPage::onMountToHost()
{
    int row = selectedDeviceRow();
    if (row < 0)
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个设备", 2500);
        return;
    }

    QString instanceId = _deviceModel->item(row, 0)->data(Qt::UserRole + 1).toString();
    QString locationPath = _deviceModel->item(row, 0)->data(Qt::UserRole).toString();

    if (locationPath.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "无法获取设备位置路径", 2500);
        return;
    }

    HyperVManager::getInstance()->mountHostDevice(instanceId, locationPath);
    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在挂载设备到宿主机...", 2000);
}
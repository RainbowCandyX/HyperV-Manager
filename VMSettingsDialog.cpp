#include "VMSettingsDialog.h"
#include "HyperVManager.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaMessageBar.h"
#include "ElaPivot.h"
#include "ElaPushButton.h"
#include "ElaTheme.h"
#include "ElaScrollPageArea.h"
#include "ElaSpinBox.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToggleSwitch.h"

static bool validateVmName(const QString& name, QString *reason)
{
    if (name.isEmpty())
    {
        *reason = "虚拟机名称不能为空";
        return false;
    }
    static const QRegularExpression invalidChars(R"([\\/:*?"<>|])");
    if (name.contains(invalidChars))
    {
        *reason = "虚拟机名称包含非法字符：\\ / : * ? \" < > |";
        return false;
    }
    for (const QChar c : name)
    {
        if (c.isSpace()) continue;
        if (c.isPrint()) continue;
        *reason = "虚拟机名称不能包含控制字符";
        return false;
    }
    return true;
}

QWidget* VMSettingsDialog::createFormRow(const QString& label, QWidget *field)
{
    ElaScrollPageArea *area = new ElaScrollPageArea(this);
    QHBoxLayout *layout = new QHBoxLayout(area);
    ElaText *text = new ElaText(label, this);
    text->setWordWrap(false);
    text->setTextPixelSize(15);
    layout->addWidget(text);
    layout->addStretch();
    layout->addWidget(field);
    return area;
}

QWidget* VMSettingsDialog::createPageContent(const QList<QWidget*>& rows, bool showButtons)
{
    QWidget *content = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addSpacing(30);

    for (QWidget *row : rows)
    {
        layout->addWidget(row);
    }
    layout->addStretch();

    if (showButtons)
    {
        ElaPushButton *applyBtn = new ElaPushButton("应用", this);
        applyBtn->setElaIcon(ElaIconType::Check, 14);
        applyBtn->setFixedSize(100, 36);
        connect(applyBtn, &ElaPushButton::clicked, this, &VMSettingsDialog::applySettings);

        ElaPushButton *cancelBtn = new ElaPushButton("取消", this);
        cancelBtn->setElaIcon(ElaIconType::Xmark, 14);
        cancelBtn->setFixedSize(100, 36);
        connect(cancelBtn, &ElaPushButton::clicked, this, &QWidget::close);

        QHBoxLayout *btnLayout = new QHBoxLayout();
        btnLayout->addStretch();
        btnLayout->addWidget(cancelBtn);
        btnLayout->addWidget(applyBtn);
        layout->addLayout(btnLayout);
        layout->addSpacing(10);
    }
    return content;
}

VMSettingsDialog::VMSettingsDialog(const QString& vmName, QWidget *parent)
    : ElaWidget(parent), _vmName(vmName)
{
    setWindowTitle(QString("虚拟机设置 - %1").arg(vmName));
    resize(900, 600);
    setWindowModality(Qt::ApplicationModal);
    setAttribute(Qt::WA_DeleteOnClose);
    setIsStayTop(false);
    setWindowButtonFlag(ElaAppBarType::ThemeChangeButtonHint, true);
    connect(this, &ElaWidget::themeChangeButtonClicked, this, []
    {
        ElaThemeType::ThemeMode mode = eTheme->getThemeMode();
        eTheme->setThemeMode(mode == ElaThemeType::Light ? ElaThemeType::Dark : ElaThemeType::Light);
    });

    _pivot = new ElaPivot(this);
    _pivot->setTextPixelSize(16);
    _pivot->setPivotSpacing(20);
    _pivot->appendPivot("常规");
    _pivot->appendPivot("处理器");
    _pivot->appendPivot("内存");
    _pivot->appendPivot("存储");
    _pivot->appendPivot("网络");
    _pivot->appendPivot("高级处理器");
    _pivot->appendPivot("高级内存");
    _pivot->appendPivot("GPU-P");
    _pivot->appendPivot("管理");

    _stackedWidget = new QStackedWidget(this);

    connect(_pivot, &ElaPivot::pivotClicked, _stackedWidget, &QStackedWidget::setCurrentIndex);

    _nameEdit = new ElaLineEdit(this);
    _nameEdit->setFixedWidth(250);

    _notesEdit = new ElaLineEdit(this);
    _notesEdit->setFixedWidth(250);
    _notesEdit->setPlaceholderText("输入备注");

    QWidget *generalContent = createPageContent({
                                                    createFormRow("虚拟机名称", _nameEdit),
                                                    createFormRow("备注", _notesEdit)
                                                }, true);
    _stackedWidget->addWidget(generalContent);

    _cpuSpinBox = new ElaSpinBox(this);
    _cpuSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _cpuSpinBox->setRange(1, 128);
    _cpuSpinBox->setValue(1);
    _cpuSpinBox->setFixedWidth(120);

    QWidget *cpuContent = createPageContent({
                                                createFormRow("处理器数量", _cpuSpinBox)
                                            }, true);
    _stackedWidget->addWidget(cpuContent);

    _memorySpinBox = new ElaSpinBox(this);
    _memorySpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memorySpinBox->setRange(32, 1048576);
    _memorySpinBox->setValue(1024);
    _memorySpinBox->setSuffix(" MB");
    _memorySpinBox->setSingleStep(512);
    _memorySpinBox->setFixedWidth(150);

    _dynamicMemorySwitch = new ElaToggleSwitch(this);

    _memoryMinSpinBox = new ElaSpinBox(this);
    _memoryMinSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memoryMinSpinBox->setRange(32, 1048576);
    _memoryMinSpinBox->setValue(512);
    _memoryMinSpinBox->setSuffix(" MB");
    _memoryMinSpinBox->setSingleStep(512);
    _memoryMinSpinBox->setFixedWidth(150);
    _memoryMinSpinBox->setEnabled(false);

    _memoryMaxSpinBox = new ElaSpinBox(this);
    _memoryMaxSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memoryMaxSpinBox->setRange(32, 1048576);
    _memoryMaxSpinBox->setValue(1048576);
    _memoryMaxSpinBox->setSuffix(" MB");
    _memoryMaxSpinBox->setSingleStep(512);
    _memoryMaxSpinBox->setFixedWidth(150);
    _memoryMaxSpinBox->setEnabled(false);

    connect(_dynamicMemorySwitch, &ElaToggleSwitch::toggled, this, [=](bool checked)
    {
        _memoryMinSpinBox->setEnabled(_isOff && checked);
        _memoryMaxSpinBox->setEnabled(_isOff && checked);
    });

    QWidget *memContent = createPageContent(
        {
            createFormRow("启动内存", _memorySpinBox),
            createFormRow("动态内存", _dynamicMemorySwitch),
            createFormRow("最小内存", _memoryMinSpinBox),
            createFormRow("最大内存", _memoryMaxSpinBox)
        }, true);
    _stackedWidget->addWidget(memContent);

    _storageModel = new QStandardItemModel(this);
    _storageModel->setHorizontalHeaderLabels({"控制器", "槽位", "类型", "路径", "大小(MB)"});

    _storageTable = new ElaTableView(this);
    _storageTable->setModel(_storageModel);
    _storageTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _storageTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _storageTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _storageTable->setAlternatingRowColors(true);
    _storageTable->horizontalHeader()->setStretchLastSection(false);
    _storageTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    _storageTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    _storageTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    _storageTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    _storageTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    _storageTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
    _storageTable->setColumnWidth(0, 70);
    _storageTable->setColumnWidth(1, 50);
    _storageTable->setColumnWidth(2, 100);
    _storageTable->setColumnWidth(4, 90);
    _storageTable->verticalHeader()->setVisible(false);

    ElaPushButton *addDiskBtn = new ElaPushButton("添加硬盘", this);
    addDiskBtn->setElaIcon(ElaIconType::HardDrive, 14);
    addDiskBtn->setFixedSize(120, 36);
    connect(addDiskBtn, &ElaPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getSaveFileName(this, "新建虚拟硬盘", "", "虚拟硬盘 (*.vhdx)");
        if (path.isEmpty()) return;
        HyperVManager::getInstance()->addVMHardDisk(_vmName, path, 60, "SCSI");
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在添加硬盘...", 2000);
    });

    ElaPushButton *addExistingBtn = new ElaPushButton("添加已有硬盘", this);
    addExistingBtn->setElaIcon(ElaIconType::FolderOpen, 14);
    addExistingBtn->setFixedSize(140, 36);
    connect(addExistingBtn, &ElaPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, "选择虚拟硬盘", "", "虚拟硬盘 (*.vhd *.vhdx)");
        if (path.isEmpty()) return;
        HyperVManager::getInstance()->addVMExistingDisk(_vmName, path, "SCSI");
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在添加硬盘...", 2000);
    });

    ElaPushButton *addDvdBtn = new ElaPushButton("添加 DVD", this);
    addDvdBtn->setElaIcon(ElaIconType::CompactDisc, 14);
    addDvdBtn->setFixedSize(120, 36);
    connect(addDvdBtn, &ElaPushButton::clicked, this, [this]()
    {
        QString path = QFileDialog::getOpenFileName(this, "选择 ISO 文件", "", "ISO 文件 (*.iso);;所有文件 (*)");
        if (path.isEmpty()) return;
        HyperVManager::getInstance()->addVMDvdDrive(_vmName, path);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在添加 DVD 驱动器...", 2000);
    });

    ElaPushButton *removeDriveBtn = new ElaPushButton("移除选中", this);
    removeDriveBtn->setElaIcon(ElaIconType::TrashCan, 14);
    removeDriveBtn->setFixedSize(120, 36);
    connect(removeDriveBtn, &ElaPushButton::clicked, this, [this]()
    {
        QModelIndex idx = _storageTable->currentIndex();
        if (!idx.isValid())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个存储设备", 2500);
            return;
        }
        int row = idx.row();
        QString ctrlType = _storageModel->item(row, 0)->data(Qt::UserRole).toString();
        int ctrlNum = _storageModel->item(row, 0)->data(Qt::UserRole + 1).toInt();
        int ctrlLoc = _storageModel->item(row, 0)->data(Qt::UserRole + 2).toInt();
        QString diskType = _storageModel->item(row, 0)->data(Qt::UserRole + 3).toString();
        if (diskType == "DVD") ctrlType = "DVD";
        HyperVManager::getInstance()->removeVMDrive(_vmName, ctrlType, ctrlNum, ctrlLoc);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在移除...", 2000);
    });

    QWidget *storageContent = new QWidget(this);
    QVBoxLayout *storageLayout = new QVBoxLayout(storageContent);
    storageLayout->setContentsMargins(0, 0, 0, 0);
    storageLayout->addSpacing(30);
    storageLayout->addWidget(_storageTable);
    QHBoxLayout *storageBtnLayout = new QHBoxLayout();
    storageBtnLayout->addWidget(addDiskBtn);
    storageBtnLayout->addWidget(addExistingBtn);
    storageBtnLayout->addWidget(addDvdBtn);
    storageBtnLayout->addStretch();
    storageBtnLayout->addWidget(removeDriveBtn);
    storageLayout->addLayout(storageBtnLayout);
    storageLayout->addStretch();
    _stackedWidget->addWidget(storageContent);

    _networkModel = new QStandardItemModel(this);
    _networkModel->setHorizontalHeaderLabels({"适配器名称", "交换机", "MAC 地址", "IP 地址"});

    _networkTable = new ElaTableView(this);
    _networkTable->setModel(_networkModel);
    _networkTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _networkTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _networkTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _networkTable->setAlternatingRowColors(true);
    _networkTable->horizontalHeader()->setStretchLastSection(true);
    _networkTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _networkTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    _networkTable->verticalHeader()->setVisible(false);
    _networkTable->setMaximumHeight(150);

    _netSwitchCombo = new ElaComboBox(this);
    _netSwitchCombo->addItem("加载中...");
    _netSwitchCombo->setEnabled(false);
    _netSwitchCombo->setFixedWidth(200);

    ElaPushButton *addAdapterBtn = new ElaPushButton("添加适配器", this);
    addAdapterBtn->setElaIcon(ElaIconType::CirclePlus, 14);
    addAdapterBtn->setFixedSize(130, 36);
    connect(addAdapterBtn, &ElaPushButton::clicked, this, [this]()
    {
        QString switchName;
        if (_netSwitchCombo->currentIndex() > 0)
        {
            switchName = _netSwitchCombo->currentText();
        }
        HyperVManager::getInstance()->addVMNetworkAdapter(_vmName, switchName);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在添加网络适配器...", 2000);
    });

    ElaPushButton *removeAdapterBtn = new ElaPushButton("移除适配器", this);
    removeAdapterBtn->setElaIcon(ElaIconType::TrashCan, 14);
    removeAdapterBtn->setFixedSize(130, 36);
    connect(removeAdapterBtn, &ElaPushButton::clicked, this, [this]()
    {
        QModelIndex idx = _networkTable->currentIndex();
        if (!idx.isValid())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个网络适配器", 2500);
            return;
        }
        QString adapterName = _networkModel->item(idx.row(), 0)->text();
        HyperVManager::getInstance()->removeVMNetworkAdapter(_vmName, adapterName);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在移除适配器...", 2000);
    });

    _netDetailWidget = new QWidget(this);
    QVBoxLayout *detailLayout = new QVBoxLayout(_netDetailWidget);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(6);

    _netSelectedLabel = new ElaText("选择一个适配器以配置", this);
    _netSelectedLabel->setTextPixelSize(14);
    detailLayout->addWidget(_netSelectedLabel);

    ElaText *qosTitle = new ElaText("流量控制 (QoS)", this);
    qosTitle->setTextPixelSize(13);
    detailLayout->addWidget(qosTitle);

    _bwLimitSpinBox = new ElaSpinBox(this);
    _bwLimitSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _bwLimitSpinBox->setRange(0, 100000);
    _bwLimitSpinBox->setValue(0);
    _bwLimitSpinBox->setSuffix(" Mbps");
    _bwLimitSpinBox->setSingleStep(100);
    _bwLimitSpinBox->setFixedWidth(150);
    _bwLimitSpinBox->setEnabled(false);

    _bwReserveSpinBox = new ElaSpinBox(this);
    _bwReserveSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _bwReserveSpinBox->setRange(0, 100000);
    _bwReserveSpinBox->setValue(0);
    _bwReserveSpinBox->setSuffix(" Mbps");
    _bwReserveSpinBox->setSingleStep(100);
    _bwReserveSpinBox->setFixedWidth(150);
    _bwReserveSpinBox->setEnabled(false);

    ElaPushButton *applyQosBtn = new ElaPushButton("应用 QoS", this);
    applyQosBtn->setElaIcon(ElaIconType::Check, 14);
    applyQosBtn->setFixedSize(110, 34);
    connect(applyQosBtn, &ElaPushButton::clicked, this, [this]()
    {
        QModelIndex idx = _networkTable->currentIndex();
        if (!idx.isValid() || idx.row() >= _cachedAdapters.size()) return;
        const QString adapterName = _cachedAdapters[idx.row()].adapterName;
        HyperVManager::getInstance()->applyVMNetworkAdapterQos(
            _vmName, adapterName, _bwLimitSpinBox->value(), _bwReserveSpinBox->value());
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在应用 QoS 设置...", 2000);
    });

    QHBoxLayout *qosRow = new QHBoxLayout();
    ElaText *limitLabel = new ElaText("上限", this);
    limitLabel->setTextPixelSize(13);
    ElaText *reserveLabel = new ElaText("下限", this);
    reserveLabel->setTextPixelSize(13);
    qosRow->addWidget(limitLabel);
    qosRow->addWidget(_bwLimitSpinBox);
    qosRow->addSpacing(20);
    qosRow->addWidget(reserveLabel);
    qosRow->addWidget(_bwReserveSpinBox);
    qosRow->addStretch();
    qosRow->addWidget(applyQosBtn);
    detailLayout->addLayout(qosRow);

    ElaText *offloadTitle = new ElaText("硬件加速", this);
    offloadTitle->setTextPixelSize(13);
    detailLayout->addWidget(offloadTitle);

    _vmqSwitch = new ElaToggleSwitch(this);
    _vmqSwitch->setEnabled(false);
    _ipsecSwitch = new ElaToggleSwitch(this);
    _ipsecSwitch->setEnabled(false);
    _sriovSwitch = new ElaToggleSwitch(this);
    _sriovSwitch->setEnabled(false);

    ElaPushButton *applyOffloadBtn = new ElaPushButton("应用加速", this);
    applyOffloadBtn->setElaIcon(ElaIconType::Check, 14);
    applyOffloadBtn->setFixedSize(110, 34);
    connect(applyOffloadBtn, &ElaPushButton::clicked, this, [this]()
    {
        QModelIndex idx = _networkTable->currentIndex();
        if (!idx.isValid() || idx.row() >= _cachedAdapters.size()) return;
        const QString adapterName = _cachedAdapters[idx.row()].adapterName;
        HyperVManager::getInstance()->applyVMNetworkAdapterOffload(
            _vmName, adapterName,
            _vmqSwitch->getIsToggled(), _ipsecSwitch->getIsToggled(), _sriovSwitch->getIsToggled());
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在应用硬件加速设置...", 2000);
    });

    QHBoxLayout *offloadRow = new QHBoxLayout();
    auto addOffloadItem = [&](const QString& label, ElaToggleSwitch *sw)
    {
        ElaText *t = new ElaText(label, this);
        t->setTextPixelSize(13);
        offloadRow->addWidget(t);
        offloadRow->addWidget(sw);
        offloadRow->addSpacing(15);
    };
    addOffloadItem("虚拟机队列", _vmqSwitch);
    addOffloadItem("IPsec任务卸载", _ipsecSwitch);
    addOffloadItem("SR-IOV", _sriovSwitch);
    offloadRow->addStretch();
    offloadRow->addWidget(applyOffloadBtn);
    detailLayout->addLayout(offloadRow);

    ElaText *secTitle = new ElaText("安全", this);
    secTitle->setTextPixelSize(13);
    detailLayout->addWidget(secTitle);

    _macSpoofSwitch = new ElaToggleSwitch(this);
    _macSpoofSwitch->setEnabled(false);
    _dhcpGuardSwitch = new ElaToggleSwitch(this);
    _dhcpGuardSwitch->setEnabled(false);
    _routerGuardSwitch = new ElaToggleSwitch(this);
    _routerGuardSwitch->setEnabled(false);

    ElaPushButton *applySecBtn = new ElaPushButton("应用安全", this);
    applySecBtn->setElaIcon(ElaIconType::Check, 14);
    applySecBtn->setFixedSize(110, 34);
    connect(applySecBtn, &ElaPushButton::clicked, this, [this]()
    {
        QModelIndex idx = _networkTable->currentIndex();
        if (!idx.isValid() || idx.row() >= _cachedAdapters.size()) return;
        const QString adapterName = _cachedAdapters[idx.row()].adapterName;
        HyperVManager::getInstance()->setVMNetworkAdapterSecurity(
            _vmName, adapterName,
            _macSpoofSwitch->getIsToggled(), _dhcpGuardSwitch->getIsToggled(), _routerGuardSwitch->getIsToggled());
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在应用安全设置...", 2000);
    });

    QHBoxLayout *secRow = new QHBoxLayout();
    auto addSecItem = [&](const QString& label, ElaToggleSwitch *sw)
    {
        ElaText *t = new ElaText(label, this);
        t->setTextPixelSize(13);
        secRow->addWidget(t);
        secRow->addWidget(sw);
        secRow->addSpacing(15);
    };
    addSecItem("MAC 欺骗", _macSpoofSwitch);
    addSecItem("DHCP 守护", _dhcpGuardSwitch);
    addSecItem("路由器守护", _routerGuardSwitch);
    secRow->addStretch();
    secRow->addWidget(applySecBtn);
    detailLayout->addLayout(secRow);

    connect(_networkTable->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current, const QModelIndex&)
            {
                if (!current.isValid() || current.row() >= _cachedAdapters.size())
                {
                    _netSelectedLabel->setText("选择一个适配器以配置");
                    _bwLimitSpinBox->setEnabled(false);
                    _bwReserveSpinBox->setEnabled(false);
                    _vmqSwitch->setEnabled(false);
                    _ipsecSwitch->setEnabled(false);
                    _sriovSwitch->setEnabled(false);
                    _macSpoofSwitch->setEnabled(false);
                    _dhcpGuardSwitch->setEnabled(false);
                    _routerGuardSwitch->setEnabled(false);
                    return;
                }
                const VMNetworkAdapterInfo& a = _cachedAdapters[current.row()];
                _netSelectedLabel->setText(QString("适配器：%1").arg(a.adapterName));
                _bwLimitSpinBox->setValue(static_cast<int>(a.bandwidthLimitMbps));
                _bwReserveSpinBox->setValue(static_cast<int>(a.bandwidthReservationMbps));
                _bwLimitSpinBox->setEnabled(true);
                _bwReserveSpinBox->setEnabled(true);
                _vmqSwitch->setIsToggled(a.vmqEnabled);
                _ipsecSwitch->setIsToggled(a.ipsecOffloadEnabled);
                _sriovSwitch->setIsToggled(a.sriovEnabled);
                _vmqSwitch->setEnabled(true);
                _ipsecSwitch->setEnabled(true);
                _sriovSwitch->setEnabled(true);
                _macSpoofSwitch->setIsToggled(a.macSpoofing);
                _dhcpGuardSwitch->setIsToggled(a.dhcpGuard);
                _routerGuardSwitch->setIsToggled(a.routerGuard);
                _macSpoofSwitch->setEnabled(true);
                _dhcpGuardSwitch->setEnabled(true);
                _routerGuardSwitch->setEnabled(true);
            });

    QWidget *networkContent = new QWidget(this);
    QVBoxLayout *networkLayout = new QVBoxLayout(networkContent);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->addSpacing(30);
    networkLayout->addWidget(_networkTable);

    ElaScrollPageArea *switchArea = new ElaScrollPageArea(this);
    QHBoxLayout *switchLayout = new QHBoxLayout(switchArea);
    ElaText *switchLabel = new ElaText("连接交换机", this);
    switchLabel->setTextPixelSize(15);
    switchLayout->addWidget(switchLabel);
    switchLayout->addStretch();
    switchLayout->addWidget(_netSwitchCombo);
    networkLayout->addWidget(switchArea);

    QHBoxLayout *netBtnLayout = new QHBoxLayout();
    netBtnLayout->addWidget(addAdapterBtn);
    netBtnLayout->addStretch();
    netBtnLayout->addWidget(removeAdapterBtn);
    networkLayout->addLayout(netBtnLayout);

    networkLayout->addSpacing(10);
    networkLayout->addWidget(_netDetailWidget);
    networkLayout->addStretch();
    _stackedWidget->addWidget(networkContent);

    _cpuReserveSpinBox = new ElaSpinBox(this);
    _cpuReserveSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _cpuReserveSpinBox->setRange(0, 100);
    _cpuReserveSpinBox->setValue(0);
    _cpuReserveSpinBox->setSuffix(" %");
    _cpuReserveSpinBox->setFixedWidth(120);

    _cpuLimitSpinBox = new ElaSpinBox(this);
    _cpuLimitSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _cpuLimitSpinBox->setRange(0, 100);
    _cpuLimitSpinBox->setValue(100);
    _cpuLimitSpinBox->setSuffix(" %");
    _cpuLimitSpinBox->setFixedWidth(120);

    _cpuWeightSpinBox = new ElaSpinBox(this);
    _cpuWeightSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _cpuWeightSpinBox->setRange(0, 10000);
    _cpuWeightSpinBox->setValue(100);
    _cpuWeightSpinBox->setFixedWidth(120);

    _nestedVirtSwitch = new ElaToggleSwitch(this);
    _hideHypervisorSwitch = new ElaToggleSwitch(this);

    ElaPushButton *applyProcBtn = new ElaPushButton("应用处理器设置", this);
    applyProcBtn->setElaIcon(ElaIconType::Check, 14);
    applyProcBtn->setFixedSize(160, 36);
    connect(applyProcBtn, &ElaPushButton::clicked, this, [this]()
    {
        VMProcessorAdvancedSettings s;
        s.count = _cpuSpinBox->value();
        s.reserve = _cpuReserveSpinBox->value();
        s.limit = _cpuLimitSpinBox->value();
        s.weight = _cpuWeightSpinBox->value();
        s.exposeVirtualizationExtensions = _nestedVirtSwitch->getIsToggled();
        s.nestedVirtualization = s.exposeVirtualizationExtensions;
        s.hideHypervisor = _hideHypervisorSwitch->getIsToggled();
        HyperVManager::getInstance()->applyVMProcessorAdvanced(_vmName, s);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在应用处理器设置...", 2000);
    });

    QWidget *advCpuContent = new QWidget(this);
    QVBoxLayout *advCpuLayout = new QVBoxLayout(advCpuContent);
    advCpuLayout->setContentsMargins(0, 0, 0, 0);
    advCpuLayout->addSpacing(30);
    advCpuLayout->addWidget(createFormRow("CPU 预留", _cpuReserveSpinBox));
    advCpuLayout->addWidget(createFormRow("CPU 上限", _cpuLimitSpinBox));
    advCpuLayout->addWidget(createFormRow("相对权重", _cpuWeightSpinBox));
    advCpuLayout->addWidget(createFormRow("嵌套虚拟化", _nestedVirtSwitch));
    advCpuLayout->addWidget(createFormRow("宿主机资源保护", _hideHypervisorSwitch));
    advCpuLayout->addStretch();
    QHBoxLayout *procBtnLayout = new QHBoxLayout();
    procBtnLayout->addStretch();
    procBtnLayout->addWidget(applyProcBtn);
    advCpuLayout->addLayout(procBtnLayout);
    advCpuLayout->addSpacing(10);
    _stackedWidget->addWidget(advCpuContent);

    _memBufferSpinBox = new ElaSpinBox(this);
    _memBufferSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memBufferSpinBox->setRange(0, 100);
    _memBufferSpinBox->setValue(20);
    _memBufferSpinBox->setSuffix(" %");
    _memBufferSpinBox->setFixedWidth(120);

    _memWeightSpinBox = new ElaSpinBox(this);
    _memWeightSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memWeightSpinBox->setRange(0, 10000);
    _memWeightSpinBox->setValue(5000);
    _memWeightSpinBox->setFixedWidth(120);

    ElaPushButton *applyMemBtn = new ElaPushButton("应用内存设置", this);
    applyMemBtn->setElaIcon(ElaIconType::Check, 14);
    applyMemBtn->setFixedSize(160, 36);
    connect(applyMemBtn, &ElaPushButton::clicked, this, [this]()
    {
        VMMemoryAdvancedSettings s;
        s.startupMB = _memorySpinBox->value();
        s.dynamicMemoryEnabled = _dynamicMemorySwitch->getIsToggled();
        s.minimumMB = _memoryMinSpinBox->value();
        s.maximumMB = _memoryMaxSpinBox->value();
        s.buffer = _memBufferSpinBox->value();
        s.weight = _memWeightSpinBox->value();
        HyperVManager::getInstance()->applyVMMemoryAdvanced(_vmName, s);
        ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息", "正在应用内存设置...", 2000);
    });

    QWidget *advMemContent = new QWidget(this);
    QVBoxLayout *advMemLayout = new QVBoxLayout(advMemContent);
    advMemLayout->setContentsMargins(0, 0, 0, 0);
    advMemLayout->addSpacing(30);
    advMemLayout->addWidget(createFormRow("内存缓冲", _memBufferSpinBox));
    advMemLayout->addWidget(createFormRow("内存优先级", _memWeightSpinBox));
    advMemLayout->addStretch();
    QHBoxLayout *memBtnLayout = new QHBoxLayout();
    memBtnLayout->addStretch();
    memBtnLayout->addWidget(applyMemBtn);
    advMemLayout->addLayout(memBtnLayout);
    advMemLayout->addSpacing(10);
    _stackedWidget->addWidget(advMemContent);

    _gpuStatusText = new ElaText("当前状态：读取中...", this);
    _gpuStatusText->setWordWrap(true);
    _gpuStatusText->setTextPixelSize(14);

    _gpuCombo = new ElaComboBox(this);
    _gpuCombo->addItem("检测中...");
    _gpuCombo->setEnabled(false);
    _gpuCombo->setFixedWidth(420);

    _gpuPercentSpinBox = new ElaSpinBox(this);
    _gpuPercentSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _gpuPercentSpinBox->setRange(10, 100);
    _gpuPercentSpinBox->setValue(50);
    _gpuPercentSpinBox->setSingleStep(5);
    _gpuPercentSpinBox->setSuffix(" %");
    _gpuPercentSpinBox->setFixedWidth(120);

    ElaPushButton *applyGpuBtn = new ElaPushButton("应用 GPU-P", this);
    applyGpuBtn->setElaIcon(ElaIconType::Check, 14);
    applyGpuBtn->setFixedSize(130, 36);
    connect(applyGpuBtn, &ElaPushButton::clicked, this, [this]()
    {
        if (!_isOff)
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "请先关闭虚拟机再配置 GPU-P", 3000);
            return;
        }
        if (_gpuCombo->count() == 0 || _gpuCombo->itemText(0) == "无可用 GPU-P 设备"
            || _gpuCombo->itemText(0) == "检测中...")
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "当前没有可用的 GPU-P 设备", 3000);
            return;
        }
        QString instancePath = _gpuCombo->currentData().toString();
        HyperVManager::getInstance()->applyVMGpuPartitionAsync(_vmName, _gpuPercentSpinBox->value(), instancePath);
        _gpuStatusText->setText("当前状态：正在应用...");
    });

    ElaPushButton *removeGpuBtn = new ElaPushButton("移除 GPU-P", this);
    removeGpuBtn->setElaIcon(ElaIconType::TrashCan, 14);
    removeGpuBtn->setFixedSize(130, 36);
    connect(removeGpuBtn, &ElaPushButton::clicked, this, [this]()
    {
        if (!_isOff)
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "请先关闭虚拟机再移除 GPU-P", 3000);
            return;
        }
        HyperVManager::getInstance()->removeVMGpuPartitionAsync(_vmName);
        _gpuStatusText->setText("当前状态：正在移除...");
    });

    ElaPushButton *installDriverBtn = new ElaPushButton("安装 GPU 驱动", this);
    installDriverBtn->setElaIcon(ElaIconType::Download, 14);
    installDriverBtn->setFixedSize(150, 36);

    _gpuDriverLogText = new ElaText("", this);
    _gpuDriverLogText->setWordWrap(true);
    _gpuDriverLogText->setTextPixelSize(12);
    _gpuDriverLogText->setStyleSheet("color: #888;");
    _gpuDriverLogText->setVisible(false);

    connect(installDriverBtn, &ElaPushButton::clicked, this, [this, installDriverBtn]()
    {
        installDriverBtn->setEnabled(false);
        _gpuDriverLogText->setVisible(true);
        _gpuDriverLogText->setText("正在安装 GPU 驱动，请稍候...\n"
            "（将挂载虚拟磁盘、同步驱动文件、创建符号链接）\n"
            "注意：虚拟机必须处于关闭状态。");
        QPointer<VMSettingsDialog> guard(this);
        HyperVManager::getInstance()->installGpuDriversAsync(_vmName,
                                                             [guard, installDriverBtn](bool success, const QString& message)
                                                             {
                                                                 if (!guard) return;
                                                                 installDriverBtn->setEnabled(true);
                                                                 if (success)
                                                                 {
                                                                     guard->_gpuDriverLogText->setText("驱动安装完成！\n" + message);
                                                                     ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功",
                                                                                            "GPU 驱动已安装到虚拟机", 3000);
                                                                 }
                                                                 else
                                                                 {
                                                                     guard->_gpuDriverLogText->setText("驱动安装失败：\n" + message);
                                                                     ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                                                                          "GPU 驱动安装失败", 5000);
                                                                 }
                                                             });
    });

    QWidget *gpuContent = new QWidget(this);
    QVBoxLayout *gpuLayout = new QVBoxLayout(gpuContent);
    gpuLayout->setContentsMargins(0, 0, 0, 0);
    gpuLayout->addSpacing(30);
    gpuLayout->addWidget(_gpuStatusText);
    gpuLayout->addSpacing(10);
    gpuLayout->addWidget(createFormRow("GPU 设备", _gpuCombo));
    gpuLayout->addWidget(createFormRow("资源分配", _gpuPercentSpinBox));
    gpuLayout->addSpacing(15);
    gpuLayout->addWidget(_gpuDriverLogText);
    gpuLayout->addStretch();
    QHBoxLayout *gpuBtnLayout = new QHBoxLayout();
    gpuBtnLayout->addWidget(installDriverBtn);
    gpuBtnLayout->addStretch();
    gpuBtnLayout->addWidget(removeGpuBtn);
    gpuBtnLayout->addWidget(applyGpuBtn);
    gpuLayout->addLayout(gpuBtnLayout);
    gpuLayout->addSpacing(10);
    _stackedWidget->addWidget(gpuContent);

    _autoStartCombo = new ElaComboBox(this);
    _autoStartCombo->addItem("无操作", "Nothing");
    _autoStartCombo->addItem("之前运行则启动", "StartIfRunning");
    _autoStartCombo->addItem("始终自动启动", "Start");
    _autoStartCombo->setFixedWidth(180);

    _autoStopCombo = new ElaComboBox(this);
    _autoStopCombo->addItem("关闭", "TurnOff");
    _autoStopCombo->addItem("保存", "Save");
    _autoStopCombo->addItem("关机", "ShutDown");
    _autoStopCombo->setFixedWidth(180);

    _checkpointCombo = new ElaComboBox(this);
    _checkpointCombo->addItem("已禁用", "Disabled");
    _checkpointCombo->addItem("标准", "Standard");
    _checkpointCombo->addItem("生产", "Production");
    _checkpointCombo->addItem("仅生产", "ProductionOnly");
    _checkpointCombo->setFixedWidth(180);

    QWidget *mgmtContent = createPageContent(
        {
            createFormRow("自动启动操作", _autoStartCombo),
            createFormRow("自动停止操作", _autoStopCombo),
            createFormRow("检查点类型", _checkpointCombo)
        }, true);
    _stackedWidget->addWidget(mgmtContent);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 10, 20, 10);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(_pivot);
    mainLayout->addWidget(_stackedWidget);

    loadSettings();
    loadStorage();
    loadNetworkAdapters();
    loadProcessorAdvanced();
    loadMemoryAdvanced();
    loadGpuStatus();

    connect(HyperVManager::getInstance(), &HyperVManager::operationFinished, this,
            [this](const QString& name, const QString& operation, bool success, const QString& message)
            {
                if (name != _vmName) return;
                if (success)
                {
                    ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功", operation + "成功", 2000);
                    if (operation.contains("硬盘") || operation.contains("DVD") || operation.contains("磁盘"))
                        loadStorage();
                    if (operation.contains("网络") || operation.contains("适配器") ||
                        operation.contains("QoS") || operation.contains("加速") || operation.contains("安全"))
                        loadNetworkAdapters();
                    if (operation.contains("GPU-P"))
                        loadGpuStatus();
                }
                else
                {
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                         operation + "失败: " + message, 5000);
                }
            });
}

void VMSettingsDialog::loadSettings()
{
    _cpuSpinBox->setEnabled(false);
    _memorySpinBox->setEnabled(false);
    _dynamicMemorySwitch->setEnabled(false);
    _memoryMinSpinBox->setEnabled(false);
    _memoryMaxSpinBox->setEnabled(false);

    QPointer<VMSettingsDialog> guard(this);
    HyperVManager::getInstance()->getVMSettingsAsync(_vmName, [guard](const VMDetailedSettings& settings, const QString& error)
    {
        if (!guard) return;

        if (!error.isEmpty())
        {
            ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                 QString("读取虚拟机设置失败：%1").arg(error), 5000);
            guard->close();
            return;
        }

        guard->_isOff = (settings.stateValue == 3);
        guard->_nameEdit->setText(settings.name);
        guard->_notesEdit->setText(settings.notes);
        guard->_cpuSpinBox->setValue(settings.processorCount);
        guard->_memorySpinBox->setValue(settings.memoryStartupMB);
        guard->_dynamicMemorySwitch->setIsToggled(settings.dynamicMemoryEnabled);
        guard->_memoryMinSpinBox->setValue(settings.memoryMinimumMB);
        guard->_memoryMaxSpinBox->setValue(settings.memoryMaximumMB);

        for (int i = 0; i < guard->_autoStartCombo->count(); ++i)
            if (guard->_autoStartCombo->itemData(i).toString() == settings.automaticStartAction)
            {
                guard->_autoStartCombo->setCurrentIndex(i);
                break;
            }
        for (int i = 0; i < guard->_autoStopCombo->count(); ++i)
            if (guard->_autoStopCombo->itemData(i).toString() == settings.automaticStopAction)
            {
                guard->_autoStopCombo->setCurrentIndex(i);
                break;
            }
        for (int i = 0; i < guard->_checkpointCombo->count(); ++i)
            if (guard->_checkpointCombo->itemData(i).toString() == settings.checkpointType)
            {
                guard->_checkpointCombo->setCurrentIndex(i);
                break;
            }

        guard->_cpuSpinBox->setEnabled(guard->_isOff);
        guard->_memorySpinBox->setEnabled(guard->_isOff);
        guard->_dynamicMemorySwitch->setEnabled(guard->_isOff);
        const bool dynOn = guard->_dynamicMemorySwitch->getIsToggled();
        guard->_memoryMinSpinBox->setEnabled(guard->_isOff && dynOn);
        guard->_memoryMaxSpinBox->setEnabled(guard->_isOff && dynOn);
    });
}

void VMSettingsDialog::loadStorage()
{
    QPointer<VMSettingsDialog> guard(this);
    HyperVManager::getInstance()->getVMStorageAsync(_vmName, [guard](const QList<VMStorageInfo>& storage, const QString& error)
    {
        if (!guard) return;
        guard->_storageModel->removeRows(0, guard->_storageModel->rowCount());
        if (!error.isEmpty()) return;

        for (const auto& s : storage)
        {
            auto makeItem = [](const QString& text)
            {
                QStandardItem *item = new QStandardItem(text);
                item->setTextAlignment(Qt::AlignCenter);
                return item;
            };
            QList<QStandardItem*> row;
            auto *ctrlItem = makeItem(s.controllerType);
            ctrlItem->setData(s.controllerType, Qt::UserRole);
            ctrlItem->setData(s.controllerNumber, Qt::UserRole + 1);
            ctrlItem->setData(s.controllerLocation, Qt::UserRole + 2);
            ctrlItem->setData(s.diskType, Qt::UserRole + 3);
            row << ctrlItem;
            row << makeItem(QString("%1:%2").arg(s.controllerNumber).arg(s.controllerLocation));
            row << makeItem(s.diskType == "DVD" ? "DVD" : "硬盘");
            row << makeItem(s.path.isEmpty() ? "(空)" : s.path);
            row << makeItem(s.maxSizeMB > 0 ? QString("%1 / %2").arg(s.currentSizeMB).arg(s.maxSizeMB) : "-");
            guard->_storageModel->appendRow(row);
        }
    });
}

void VMSettingsDialog::loadNetworkAdapters()
{
    QPointer<VMSettingsDialog> guard(this);
    HyperVManager::getInstance()->getVirtualSwitchesAsync([guard](const QStringList& switches, const QString&)
    {
        if (!guard) return;
        guard->_netSwitchCombo->clear();
        guard->_netSwitchCombo->addItem("无");
        for (const QString& sw : switches)
            guard->_netSwitchCombo->addItem(sw);
        guard->_netSwitchCombo->setEnabled(true);
    });

    HyperVManager::getInstance()->getVMNetworkAdaptersAsync(_vmName, [guard](const QList<VMNetworkAdapterInfo>& adapters, const QString& error)
    {
        if (!guard) return;
        guard->_networkModel->removeRows(0, guard->_networkModel->rowCount());
        guard->_cachedAdapters = adapters;
        if (!error.isEmpty()) return;

        for (const auto& a : adapters)
        {
            auto makeItem = [](const QString& text)
            {
                QStandardItem *item = new QStandardItem(text);
                item->setTextAlignment(Qt::AlignCenter);
                return item;
            };
            QList<QStandardItem*> row;
            row << makeItem(a.adapterName);
            row << makeItem(a.switchName.isEmpty() ? "(未连接)" : a.switchName);
            row << makeItem(a.macAddress);
            row << makeItem(a.ipAddresses.isEmpty() ? "-" : a.ipAddresses);
            guard->_networkModel->appendRow(row);
        }
    });
}

void VMSettingsDialog::loadProcessorAdvanced()
{
    QPointer<VMSettingsDialog> guard(this);
    HyperVManager::getInstance()->getVMProcessorAdvancedAsync(_vmName, [guard](const VMProcessorAdvancedSettings& s, const QString&)
    {
        if (!guard) return;
        guard->_cpuReserveSpinBox->setValue(s.reserve);
        guard->_cpuLimitSpinBox->setValue(s.limit);
        guard->_cpuWeightSpinBox->setValue(s.weight);
        guard->_nestedVirtSwitch->setIsToggled(s.exposeVirtualizationExtensions);
        guard->_hideHypervisorSwitch->setIsToggled(s.hideHypervisor);
    });
}

void VMSettingsDialog::loadMemoryAdvanced()
{
    QPointer<VMSettingsDialog> guard(this);
    HyperVManager::getInstance()->getVMMemoryAdvancedAsync(_vmName, [guard](const VMMemoryAdvancedSettings& s, const QString&)
    {
        if (!guard) return;
        guard->_memBufferSpinBox->setValue(s.buffer);
        guard->_memWeightSpinBox->setValue(s.weight);
    });
}

void VMSettingsDialog::loadGpuStatus()
{
    QPointer<VMSettingsDialog> guard(this);

    HyperVManager::getInstance()->getPartitionableGpusAsync([guard](const QList<GpuPartitionableInfo>& gpus, const QString&)
    {
        if (!guard) return;
        guard->_gpuCombo->clear();
        if (gpus.isEmpty())
        {
            guard->_gpuCombo->addItem("无可用 GPU-P 设备");
        }
        else
        {
            guard->_gpuCombo->addItem("自动选择首个可用 GPU", "");
            for (const auto& gpu : gpus)
            {
                QString display = gpu.name.isEmpty() ? gpu.instancePath : gpu.name;
                if (!gpu.validPartitionCounts.isEmpty())
                    display += QString(" (分区: %1)").arg(gpu.validPartitionCounts);
                guard->_gpuCombo->addItem(display, gpu.instancePath);
            }
            guard->_gpuCombo->setEnabled(true);
        }
    });

    HyperVManager::getInstance()->getVMGpuPartitionStatusAsync(_vmName, [guard](const VMGpuPartitionStatus& status, const QString& error)
    {
        if (!guard) return;
        if (!error.isEmpty())
        {
            guard->_gpuStatusText->setText("当前状态：读取失败");
            return;
        }
        if (!status.enabled)
        {
            guard->_gpuStatusText->setText("当前状态：未启用 GPU-P");
            return;
        }
        int percent = status.allocationPercent > 0 ? status.allocationPercent : 50;
        guard->_gpuPercentSpinBox->setValue(percent);
        guard->_gpuStatusText->setText(QString("当前状态：已启用 GPU-P（约 %1%）").arg(percent));

        if (!status.instancePath.isEmpty())
        {
            for (int i = 0; i < guard->_gpuCombo->count(); ++i)
            {
                if (guard->_gpuCombo->itemData(i).toString() == status.instancePath)
                {
                    guard->_gpuCombo->setCurrentIndex(i);
                    break;
                }
            }
        }
    });
}

void VMSettingsDialog::applySettings()
{
    VMDetailedSettings settings;
    settings.name = _nameEdit->text().trimmed();
    QString reason;
    if (!validateVmName(settings.name, &reason))
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", reason, 2500);
        return;
    }
    settings.stateValue = _isOff ? 3 : 2;
    settings.processorCount = _cpuSpinBox->value();
    settings.memoryStartupMB = _memorySpinBox->value();
    settings.dynamicMemoryEnabled = _dynamicMemorySwitch->getIsToggled();
    settings.memoryMinimumMB = _memoryMinSpinBox->value();
    settings.memoryMaximumMB = _memoryMaxSpinBox->value();
    settings.notes = _notesEdit->text();
    settings.automaticStartAction = _autoStartCombo->currentData().toString();
    settings.automaticStopAction = _autoStopCombo->currentData().toString();
    settings.checkpointType = _checkpointCombo->currentData().toString();

    if (_isOff && settings.dynamicMemoryEnabled)
    {
        if (settings.memoryMinimumMB > settings.memoryStartupMB)
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "动态内存配置不合法：最小内存不能大于启动内存", 3000);
            return;
        }
        if (settings.memoryStartupMB > settings.memoryMaximumMB)
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "动态内存配置不合法：启动内存不能大于最大内存", 3000);
            return;
        }
    }

    HyperVManager::getInstance()->applyVMSettings(_vmName, settings);
    close();
}
#include "GpuSettingsPage.h"

#include "HyperVManager.h"

#include <QHBoxLayout>
#include <QPointer>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaSpinBox.h"
#include "ElaText.h"

static QWidget* createFormRow(const QString& label, QWidget *field, QWidget *parent)
{
    ElaScrollPageArea *area = new ElaScrollPageArea(parent);
    QHBoxLayout *layout = new QHBoxLayout(area);
    ElaText *text = new ElaText(label, parent);
    text->setWordWrap(false);
    text->setTextPixelSize(15);
    layout->addWidget(text);
    layout->addStretch();
    layout->addWidget(field);
    return area;
}

GpuSettingsPage::GpuSettingsPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("GPU-P 设置");

    ElaText *titleText = new ElaText("GPU-P 设置", this);
    titleText->setWordWrap(false);
    titleText->setTextPixelSize(18);

    _vmCombo = new ElaComboBox(this);
    _vmCombo->addItem("加载中...");
    _vmCombo->setEnabled(false);
    _vmCombo->setFixedWidth(500);

    _gpuCombo = new ElaComboBox(this);
    _gpuCombo->addItem("检测中...");
    _gpuCombo->setEnabled(false);
    _gpuCombo->setFixedWidth(500);

    _gpuPercentSpinBox = new ElaSpinBox(this);
    _gpuPercentSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _gpuPercentSpinBox->setRange(10, 100);
    _gpuPercentSpinBox->setValue(50);
    _gpuPercentSpinBox->setSingleStep(5);
    _gpuPercentSpinBox->setSuffix(" %");
    _gpuPercentSpinBox->setFixedWidth(120);
    _gpuPercentSpinBox->setEnabled(false);

    _statusText = new ElaText("当前状态：读取中...", this);
    _statusText->setWordWrap(true);
    _statusText->setTextPixelSize(14);

    ElaPushButton *refreshBtn = new ElaPushButton("刷新 VM/GPU", this);
    refreshBtn->setElaIcon(ElaIconType::ArrowRotateRight, 14);
    refreshBtn->setFixedSize(140, 36);

    _applyBtn = new ElaPushButton("应用 GPU-P", this);
    _applyBtn->setElaIcon(ElaIconType::Check, 14);
    _applyBtn->setFixedSize(120, 36);
    _applyBtn->setEnabled(false);

    _removeBtn = new ElaPushButton("移除 GPU-P", this);
    _removeBtn->setElaIcon(ElaIconType::TrashCan, 14);
    _removeBtn->setFixedSize(120, 36);
    _removeBtn->setEnabled(false);

    connect(refreshBtn, &ElaPushButton::clicked, this, [this]()
    {
        HyperVManager::getInstance()->refreshVMList();
        refreshGpuList();
    });
    connect(_vmCombo, &ElaComboBox::currentIndexChanged, this, [this](int)
    {
        updateActionStates();
        refreshSelectedVmGpuStatus();
    });
    connect(_applyBtn, &ElaPushButton::clicked, this, &GpuSettingsPage::applyGpuSettings);
    connect(_removeBtn, &ElaPushButton::clicked, this, &GpuSettingsPage::removeGpuSettings);

    connect(HyperVManager::getInstance(), &HyperVManager::vmListRefreshed, this, &GpuSettingsPage::refreshVmList);
    connect(HyperVManager::getInstance(), &HyperVManager::operationFinished, this,
            [this](const QString& vmName, const QString& operation, bool, const QString&)
            {
                const QString currentVm = selectedVmName();
                if (vmName != currentVm)
                {
                    return;
                }
                if (operation.contains("GPU-P"))
                {
                    refreshSelectedVmGpuStatus();
                }
            });

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("GPU-P 设置");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addSpacing(30);
    centerLayout->addWidget(titleText);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(createFormRow("目标虚拟机", _vmCombo, this));
    centerLayout->addWidget(createFormRow("GPU 设备", _gpuCombo, this));
    centerLayout->addWidget(createFormRow("GPU 资源分配", _gpuPercentSpinBox, this));
    centerLayout->addWidget(_statusText);
    centerLayout->addSpacing(15);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addWidget(refreshBtn);
    btnLayout->addStretch();
    btnLayout->addWidget(_removeBtn);
    btnLayout->addWidget(_applyBtn);
    centerLayout->addLayout(btnLayout);
    centerLayout->addStretch();
    addCentralWidget(centralWidget, true, true, 0);

    refreshVmList();
    refreshGpuList();
}

GpuSettingsPage::~GpuSettingsPage()
{
}

void GpuSettingsPage::refreshVmList()
{
    const QString currentVm = selectedVmName();
    const auto vmList = HyperVManager::getInstance()->getVMList();

    _vmCombo->blockSignals(true);
    _vmCombo->clear();
    if (vmList.isEmpty())
    {
        _vmCombo->addItem("暂无虚拟机");
        _vmCombo->setEnabled(false);
    }
    else
    {
        for (const auto& vm : vmList)
        {
            _vmCombo->addItem(vm.name);
        }
        _vmCombo->setEnabled(true);

        int restoreIndex = -1;
        if (!currentVm.isEmpty())
        {
            for (int i = 0; i < _vmCombo->count(); ++i)
            {
                if (_vmCombo->itemText(i) == currentVm)
                {
                    restoreIndex = i;
                    break;
                }
            }
        }
        if (restoreIndex >= 0)
        {
            _vmCombo->setCurrentIndex(restoreIndex);
        }
        else
        {
            _vmCombo->setCurrentIndex(0);
        }
    }
    _vmCombo->blockSignals(false);

    updateActionStates();
    refreshSelectedVmGpuStatus();
}

void GpuSettingsPage::refreshGpuList()
{
    _gpuListReady = false;
    _gpuCombo->blockSignals(true);
    _gpuCombo->clear();
    _gpuCombo->addItem("检测中...");
    _gpuCombo->setEnabled(false);
    _gpuCombo->blockSignals(false);
    updateActionStates();

    QPointer<GpuSettingsPage> guard(this);
    HyperVManager::getInstance()->getPartitionableGpusAsync([guard](const QList<GpuPartitionableInfo>& gpus, const QString& error)
    {
        if (!guard)
        {
            return;
        }

        guard->_gpuListReady = true;
        guard->_gpuCombo->blockSignals(true);
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
                QString display = gpu.name;
                if (display.isEmpty())
                {
                    display = gpu.instancePath;
                }
                if (!gpu.validPartitionCounts.isEmpty())
                {
                    display += QString(" (支持分区: %1)").arg(gpu.validPartitionCounts);
                }
                guard->_gpuCombo->addItem(display, gpu.instancePath);
            }
            guard->_gpuCombo->setCurrentIndex(0);
        }
        guard->_gpuCombo->blockSignals(false);

        guard->updateActionStates();
        guard->refreshSelectedVmGpuStatus();

        if (!error.isEmpty())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   QString("检测 GPU-P 设备失败：%1").arg(error), 5000);
        }
    });
}

void GpuSettingsPage::refreshSelectedVmGpuStatus()
{
    const QString vmName = selectedVmName();
    if (vmName.isEmpty())
    {
        _statusText->setText("当前状态：请先选择虚拟机");
        return;
    }

    _statusText->setText("当前状态：读取中...");
    QPointer<GpuSettingsPage> guard(this);
    HyperVManager::getInstance()->getVMGpuPartitionStatusAsync(vmName, [guard, vmName](const VMGpuPartitionStatus& status, const QString& error)
    {
        if (!guard)
        {
            return;
        }
        if (guard->selectedVmName() != vmName)
        {
            return;
        }

        if (!error.isEmpty())
        {
            guard->_statusText->setText(QString("当前状态：读取失败（%1）").arg(error));
            return;
        }

        if (!status.enabled)
        {
            guard->_statusText->setText("当前状态：未启用 GPU-P");
            return;
        }

        const int percent = status.allocationPercent > 0 ? status.allocationPercent : 50;
        guard->_gpuPercentSpinBox->setValue(percent);
        guard->_statusText->setText(QString("当前状态：已启用 GPU-P（约 %1%）").arg(percent));

        if (!status.instancePath.isEmpty() && guard->_gpuCombo->count() > 0)
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

void GpuSettingsPage::applyGpuSettings()
{
    const QString vmName = selectedVmName();
    if (vmName.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择虚拟机", 2500);
        return;
    }
    if (!_gpuListReady || _gpuCombo->count() == 0 || _gpuCombo->itemText(0) == "无可用 GPU-P 设备")
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "当前没有可用的 GPU-P 设备", 3000);
        return;
    }

    for (const auto& vm : HyperVManager::getInstance()->getVMList())
    {
        if (vm.name == vmName && vm.state != "Off")
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "请先关闭虚拟机再配置 GPU-P", 3000);
            return;
        }
    }

    const QString instancePath = _gpuCombo->currentData().toString();
    HyperVManager::getInstance()->applyVMGpuPartitionAsync(vmName, _gpuPercentSpinBox->value(), instancePath);
    _statusText->setText("当前状态：正在应用 GPU-P 设置...");
}

void GpuSettingsPage::removeGpuSettings()
{
    const QString vmName = selectedVmName();
    if (vmName.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择虚拟机", 2500);
        return;
    }

    for (const auto& vm : HyperVManager::getInstance()->getVMList())
    {
        if (vm.name == vmName && vm.state != "Off")
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "请先关闭虚拟机再移除 GPU-P", 3000);
            return;
        }
    }

    HyperVManager::getInstance()->removeVMGpuPartitionAsync(vmName);
    _statusText->setText("当前状态：正在移除 GPU-P...");
}

void GpuSettingsPage::updateActionStates()
{
    const QString vmName = selectedVmName();
    const bool hasVm = !vmName.isEmpty();
    const bool hasGpuChoices = _gpuListReady && _gpuCombo->count() > 0 &&
                               _gpuCombo->itemText(0) != "无可用 GPU-P 设备" &&
                               _gpuCombo->itemText(0) != "检测中...";

    bool vmIsOff = false;
    if (hasVm)
    {
        for (const auto& vm : HyperVManager::getInstance()->getVMList())
        {
            if (vm.name == vmName)
            {
                vmIsOff = (vm.state == "Off");
                break;
            }
        }
    }

    _gpuCombo->setEnabled(hasVm && vmIsOff && hasGpuChoices);
    _gpuPercentSpinBox->setEnabled(hasVm && vmIsOff && hasGpuChoices);
    _applyBtn->setEnabled(hasVm && vmIsOff && hasGpuChoices);
    _removeBtn->setEnabled(hasVm && vmIsOff);

    if (hasVm && !vmIsOff)
    {
        _statusText->setText("当前状态：请先关闭虚拟机再配置 GPU-P");
    }
}

QString GpuSettingsPage::selectedVmName() const
{
    if (!_vmCombo || _vmCombo->count() == 0)
    {
        return {};
    }
    const QString text = _vmCombo->currentText();
    if (text.isEmpty() || text == "暂无虚拟机" || text == "加载中...")
    {
        return {};
    }
    return text;
}

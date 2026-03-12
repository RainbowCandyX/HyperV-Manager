#include "CreateVMPage.h"
#include "HyperVManager.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QPointer>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaSpinBox.h"
#include "ElaText.h"
#include "ElaToggleSwitch.h"

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

static bool validateVmName(const QString& rawName, QString *reason)
{
    if (rawName.isEmpty())
    {
        *reason = "请输入虚拟机名称";
        return false;
    }
    if (rawName.startsWith(' ') || rawName.startsWith('\t'))
    {
        *reason = "虚拟机名称不能以空格或制表符开头";
        return false;
    }
    if (rawName.endsWith(' ') || rawName.endsWith('\t'))
    {
        *reason = "虚拟机名称不能以空格或制表符结尾";
        return false;
    }
    if (rawName.endsWith('.'))
    {
        *reason = "虚拟机名称不能以句点结尾";
        return false;
    }
    if (rawName.length() > 100)
    {
        *reason = "虚拟机名称不能超过 100 个字符";
        return false;
    }
    static const QRegularExpression invalidChars(R"([\\/:*?"<>|])");
    if (rawName.contains(invalidChars))
    {
        *reason = "虚拟机名称包含非法字符：\\ / : * ? \" < > |";
        return false;
    }
    for (const QChar c : rawName)
    {
        if (c.isSpace()) continue;
        if (c.isPrint()) continue;
        *reason = "虚拟机名称不能包含控制字符";
        return false;
    }
    return true;
}

static bool validateVhdPath(const QString& path, QString *reason)
{
    if (path.isEmpty())
    {
        *reason = "请指定虚拟硬盘路径";
        return false;
    }
    QFileInfo fileInfo(path);
    if (!fileInfo.isAbsolute())
    {
        *reason = "虚拟硬盘路径必须是绝对路径";
        return false;
    }
    const QString suffix = fileInfo.suffix().toLower();
    if (suffix != "vhd" && suffix != "vhdx")
    {
        *reason = "虚拟硬盘文件扩展名需为 .vhd 或 .vhdx";
        return false;
    }
    QFileInfo dirInfo(fileInfo.absolutePath());
    if (!dirInfo.exists() || !dirInfo.isDir())
    {
        *reason = "虚拟硬盘所在目录不存在";
        return false;
    }
    if (fileInfo.exists())
    {
        *reason = "虚拟硬盘文件已存在，请更换路径";
        return false;
    }
    return true;
}

CreateVMPage::CreateVMPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("新建虚拟机");

    ElaText *titleText = new ElaText("创建新虚拟机", this);
    titleText->setWordWrap(false);
    titleText->setTextPixelSize(18);

    _nameEdit = new ElaLineEdit(this);
    _nameEdit->setPlaceholderText("输入虚拟机名称");
    _nameEdit->setFixedWidth(250);

    _generationCombo = new ElaComboBox(this);
    _generationCombo->addItem("第 1 代 (BIOS + MBR)");
    _generationCombo->addItem("第 2 代 (UEFI + GPT)");
    _generationCombo->setCurrentIndex(1);
    _generationCombo->setMinimumWidth(200);
    connect(_generationCombo, &ElaComboBox::currentIndexChanged, this, &CreateVMPage::updateSecurityControls);

    _generationDescText = new ElaText(this);
    _generationDescText->setTextPixelSize(12);
    _generationDescText->setWordWrap(true);
    auto updateGenDesc = [this]()
    {
        if (_generationCombo->currentIndex() == 0)
            _generationDescText->setText("第 1 代：兼容旧版操作系统，支持 IDE 磁盘和旧式网卡，不支持安全启动和 TPM");
        else
            _generationDescText->setText("第 2 代：支持安全启动、TPM、SCSI 磁盘和 PXE 网络启动，推荐用于 Windows 10/11 及现代 Linux");
    };
    updateGenDesc();
    connect(_generationCombo, &ElaComboBox::currentIndexChanged, this, updateGenDesc);

    _versionCombo = new ElaComboBox(this);
    _versionCombo->addItem("默认", "");
    _versionCombo->setFixedWidth(120);
    QPointer<CreateVMPage> versionGuard(this);
    HyperVManager::getInstance()->getSupportedVersionsAsync([versionGuard](const QStringList& versions, const QString&)
    {
        if (!versionGuard) return;
        for (const QString& v : versions)
        {
            versionGuard->_versionCombo->addItem(v, v);
        }
    });

    _memorySpinBox = new ElaSpinBox(this);
    _memorySpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _memorySpinBox->setRange(512, 65536);
    _memorySpinBox->setValue(4096);
    _memorySpinBox->setSuffix(" MB");
    _memorySpinBox->setSingleStep(512);
    _memorySpinBox->setFixedWidth(120);

    _dynamicMemorySwitch = new ElaToggleSwitch(this);
    _dynamicMemorySwitch->setIsToggled(false);

    _vhdPathEdit = new ElaLineEdit(this);
    _vhdPathEdit->setPlaceholderText("C:\\Hyper-V\\VMs\\disk.vhdx");
    _vhdPathEdit->setFixedWidth(250);

    QWidget *vhdPathWidget = new QWidget(this);
    QHBoxLayout *vhdPathLayout = new QHBoxLayout(vhdPathWidget);
    vhdPathLayout->setContentsMargins(0, 0, 0, 0);
    vhdPathLayout->addWidget(_vhdPathEdit);
    ElaPushButton *browseBtn = new ElaPushButton("浏览", this);
    browseBtn->setFixedWidth(60);
    connect(browseBtn, &ElaPushButton::clicked, this, [=]()
    {
        QString path = QFileDialog::getSaveFileName(this, "选择 VHD 路径", "", "虚拟硬盘 (*.vhdx)");
        if (!path.isEmpty()) _vhdPathEdit->setText(path);
    });
    vhdPathLayout->addWidget(browseBtn);

    _vhdSizeSpinBox = new ElaSpinBox(this);
    _vhdSizeSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _vhdSizeSpinBox->setRange(1, 2048);
    _vhdSizeSpinBox->setValue(60);
    _vhdSizeSpinBox->setSuffix(" GB");
    _vhdSizeSpinBox->setFixedWidth(120);

    _switchCombo = new ElaComboBox(this);
    _switchCombo->addItem("加载中...");
    _switchCombo->setEnabled(false);
    QPointer<CreateVMPage> guard(this);
    HyperVManager::getInstance()->getVirtualSwitchesAsync([guard](const QStringList& switches, const QString& error)
    {
        if (!guard) return;
        guard->_switchCombo->clear();
        guard->_switchCombo->addItem("无");
        for (const QString& sw : switches) guard->_switchCombo->addItem(sw);
        guard->_switchCombo->setEnabled(true);
        if (!error.isEmpty())
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   QString("获取虚拟交换机失败：%1").arg(error), 5000);
    });

    _isoPathEdit = new ElaLineEdit(this);
    _isoPathEdit->setPlaceholderText("选择 ISO 安装镜像（可选）");
    _isoPathEdit->setFixedWidth(250);

    QWidget *isoPathWidget = new QWidget(this);
    QHBoxLayout *isoPathLayout = new QHBoxLayout(isoPathWidget);
    isoPathLayout->setContentsMargins(0, 0, 0, 0);
    isoPathLayout->addWidget(_isoPathEdit);
    ElaPushButton *isoBrowseBtn = new ElaPushButton("浏览", this);
    isoBrowseBtn->setFixedWidth(60);
    connect(isoBrowseBtn, &ElaPushButton::clicked, this, [=]()
    {
        QString path = QFileDialog::getOpenFileName(this, "选择 ISO 文件", "", "ISO 文件 (*.iso)");
        if (!path.isEmpty()) _isoPathEdit->setText(path);
    });
    isoPathLayout->addWidget(isoBrowseBtn);

    _secureBootSwitch = new ElaToggleSwitch(this);
    _secureBootSwitch->setIsToggled(true);

    _tpmSwitch = new ElaToggleSwitch(this);
    _tpmSwitch->setIsToggled(false);

    _startAfterSwitch = new ElaToggleSwitch(this);
    _startAfterSwitch->setIsToggled(false);

    _isolationCombo = new ElaComboBox(this);
    _isolationCombo->addItem("Disabled");
    _isolationCombo->setFixedWidth(200);
    _isolationCombo->setEnabled(false);

    QPointer<CreateVMPage> isoGuard(this);
    HyperVManager::getInstance()->getIsolationSupportAsync([isoGuard](bool supported, const QStringList& types, const QString&)
    {
        if (!isoGuard) return;
        isoGuard->_isolationSupported = supported;
        isoGuard->_isolationCombo->clear();
        for (const QString& t : types)
            isoGuard->_isolationCombo->addItem(t);
        int disabledIdx = isoGuard->_isolationCombo->findText("Disabled");
        isoGuard->_isolationCombo->setCurrentIndex(disabledIdx >= 0 ? disabledIdx : 0);
        isoGuard->updateSecurityControls();
    });

    _gpuToggle = new ElaToggleSwitch(this);
    _gpuToggle->setIsToggled(false);
    _gpuToggle->setEnabled(false);

    _gpuCombo = new ElaComboBox(this);
    _gpuCombo->addItem("检测中...");
    _gpuCombo->setEnabled(false);
    _gpuCombo->setFixedWidth(320);

    _gpuPercentSpinBox = new ElaSpinBox(this);
    _gpuPercentSpinBox->setButtonMode(ElaSpinBoxType::PMSide);
    _gpuPercentSpinBox->setRange(10, 100);
    _gpuPercentSpinBox->setValue(50);
    _gpuPercentSpinBox->setSuffix(" %");
    _gpuPercentSpinBox->setSingleStep(5);
    _gpuPercentSpinBox->setFixedWidth(120);
    _gpuPercentSpinBox->setEnabled(false);

    connect(_gpuToggle, &ElaToggleSwitch::toggled, this, [this](bool) { updateGpuControls(); });

    QPointer<CreateVMPage> gpuGuard(this);
    HyperVManager::getInstance()->getPartitionableGpusAsync([gpuGuard](const QList<GpuPartitionableInfo>& gpus, const QString& error)
    {
        if (!gpuGuard) return;
        gpuGuard->_gpuListReady = true;
        gpuGuard->_gpuCombo->clear();
        if (gpus.isEmpty())
        {
            gpuGuard->_gpuCombo->addItem("无可用 GPU-P 设备");
        }
        else
        {
            gpuGuard->_gpuCombo->addItem("自动选择首个可用 GPU", "");
            for (const GpuPartitionableInfo& gpu : gpus)
            {
                QString display = gpu.name.isEmpty() ? gpu.instancePath : gpu.name;
                if (!gpu.validPartitionCounts.isEmpty())
                    display += QString(" (支持分区: %1)").arg(gpu.validPartitionCounts);
                gpuGuard->_gpuCombo->addItem(display, gpu.instancePath);
            }
        }
        if (!error.isEmpty())
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   QString("检测 GPU-P 设备失败：%1").arg(error), 5000);
        gpuGuard->updateGpuControls();
    });

    ElaPushButton *createBtn = new ElaPushButton("创建虚拟机", this);
    createBtn->setElaIcon(ElaIconType::CirclePlus, 16);
    createBtn->setFixedSize(160, 40);
    connect(createBtn, &ElaPushButton::clicked, this, &CreateVMPage::onCreateClicked);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("新建虚拟机");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addSpacing(30);
    centerLayout->addWidget(titleText);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(createFormRow("虚拟机名称", _nameEdit, this));
    {
        ElaScrollPageArea *genArea = new ElaScrollPageArea(this);
        genArea->setFixedHeight(95);
        QVBoxLayout *genOuterLayout = new QVBoxLayout(genArea);
        genOuterLayout->setContentsMargins(10, 8, 10, 8);
        genOuterLayout->setSpacing(4);
        QHBoxLayout *genTopLayout = new QHBoxLayout();
        ElaText *genLabel = new ElaText("虚拟机代数", this);
        genLabel->setWordWrap(false);
        genLabel->setTextPixelSize(15);
        genTopLayout->addWidget(genLabel);
        genTopLayout->addStretch();
        genTopLayout->addWidget(_generationCombo);
        genOuterLayout->addLayout(genTopLayout);
        genOuterLayout->addWidget(_generationDescText);
        centerLayout->addWidget(genArea);
    }
    centerLayout->addWidget(createFormRow("虚拟机版本", _versionCombo, this));
    centerLayout->addWidget(createFormRow("启动内存", _memorySpinBox, this));
    centerLayout->addWidget(createFormRow("动态内存", _dynamicMemorySwitch, this));
    centerLayout->addWidget(createFormRow("虚拟硬盘路径", vhdPathWidget, this));
    centerLayout->addWidget(createFormRow("虚拟硬盘大小", _vhdSizeSpinBox, this));
    centerLayout->addWidget(createFormRow("虚拟交换机", _switchCombo, this));
    centerLayout->addWidget(createFormRow("安装镜像 (ISO)", isoPathWidget, this));
    centerLayout->addWidget(createFormRow("安全启动", _secureBootSwitch, this));
    centerLayout->addWidget(createFormRow("TPM", _tpmSwitch, this));
    centerLayout->addWidget(createFormRow("机密计算", _isolationCombo, this));
    centerLayout->addWidget(createFormRow("创建后启动", _startAfterSwitch, this));
    centerLayout->addWidget(createFormRow("启用 GPU-P", _gpuToggle, this));
    centerLayout->addWidget(createFormRow("GPU 设备", _gpuCombo, this));
    centerLayout->addWidget(createFormRow("GPU 资源分配", _gpuPercentSpinBox, this));
    centerLayout->addSpacing(20);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(createBtn);
    btnLayout->addStretch();
    centerLayout->addLayout(btnLayout);
    centerLayout->addStretch();
    addCentralWidget(centralWidget, true, true, 0);

    updateSecurityControls();
}

CreateVMPage::~CreateVMPage()
{
}

void CreateVMPage::updateGpuControls()
{
    const bool hasGpuChoices = _gpuListReady && _gpuCombo->count() > 0 &&
                               _gpuCombo->itemText(0) != "无可用 GPU-P 设备";
    if (!hasGpuChoices && _gpuToggle->getIsToggled())
    {
        _gpuToggle->setIsToggled(false);
    }
    _gpuToggle->setEnabled(hasGpuChoices);
    const bool enabled = hasGpuChoices && _gpuToggle->getIsToggled();
    _gpuCombo->setEnabled(enabled);
    _gpuPercentSpinBox->setEnabled(enabled);
}

void CreateVMPage::updateSecurityControls()
{
    bool isGen2 = (_generationCombo->currentIndex() == 1);
    _secureBootSwitch->setEnabled(isGen2);
    _tpmSwitch->setEnabled(isGen2);
    if (!isGen2)
    {
        _secureBootSwitch->setIsToggled(false);
        _tpmSwitch->setIsToggled(false);
    }
    _isolationCombo->setEnabled(isGen2 && _isolationSupported);
    if (!isGen2 || !_isolationSupported)
    {
        _isolationCombo->setCurrentIndex(0);
    }
}

void CreateVMPage::onCreateClicked()
{
    QString name = _nameEdit->text();
    QString reason;
    if (!validateVmName(name, &reason))
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", reason, 2500);
        return;
    }

    QString vhdPath = _vhdPathEdit->text().trimmed();
    if (!validateVhdPath(vhdPath, &reason))
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", reason, 3000);
        return;
    }

    int generation = _generationCombo->currentIndex() + 1;
    QString version = _versionCombo->currentData().toString();
    qint64 memoryMB = _memorySpinBox->value();
    bool dynamicMemory = _dynamicMemorySwitch->getIsToggled();
    qint64 vhdSizeGB = _vhdSizeSpinBox->value();

    QString switchName;
    if (_switchCombo->currentIndex() > 0)
    {
        switchName = _switchCombo->currentText();
    }

    QString isoPath = _isoPathEdit->text().trimmed();
    bool secureBoot = _secureBootSwitch->isEnabled() && _secureBootSwitch->getIsToggled();
    bool enableTpm = _tpmSwitch->isEnabled() && _tpmSwitch->getIsToggled();
    bool startAfter = _startAfterSwitch->getIsToggled();
    QString isolationType = _isolationCombo->isEnabled() ? _isolationCombo->currentText() : "Disabled";

    bool enableGpuPartition = _gpuToggle->isEnabled() && _gpuToggle->getIsToggled();
    QString gpuInstancePath;
    int gpuAllocationPercent = _gpuPercentSpinBox->value();
    if (enableGpuPartition)
    {
        if (!_gpuListReady || _gpuCombo->count() == 0 || _gpuCombo->itemText(0) == "无可用 GPU-P 设备")
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "当前没有可用的 GPU-P 设备，请关闭 GPU-P 或检查宿主机配置", 3500);
            return;
        }
        gpuInstancePath = _gpuCombo->currentData().toString();
    }

    HyperVManager::getInstance()->createVMAdvanced(
        name, generation, version, memoryMB, dynamicMemory,
        vhdPath, vhdSizeGB, switchName, isoPath,
        secureBoot, enableTpm, startAfter, isolationType,
        enableGpuPartition, gpuAllocationPercent, gpuInstancePath);

    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息",
                               QString("正在创建虚拟机 %1...").arg(name), 3000);
}

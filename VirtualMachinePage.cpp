#include "VirtualMachinePage.h"
#include "HyperVManager.h"
#include "VMSettingsDialog.h"

#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QDateTime>
#include <QPointer>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include "ElaContentDialog.h"
#include "ElaLineEdit.h"
#include "ElaListView.h"
#include "ElaMenu.h"
#include "ElaMessageBar.h"
#include "ElaDrawerArea.h"
#include "ElaScrollArea.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaToolButton.h"

static const int THUMBNAIL_W = 320;
static const int THUMBNAIL_H = 240;

static QColor stateColor(const QString& state)
{
    if (state == "Running") return QColor("#4CAF50");
    if (state == "Paused") return QColor("#FF9800");
    if (state == "Saved") return QColor("#2196F3");
    return QColor("#9E9E9E");
}

static QString stateTextCn(const QString& state)
{
    if (state == "Running") return "运行中";
    if (state == "Off") return "已关闭";
    if (state == "Paused") return "已暂停";
    if (state == "Saved") return "已保存";
    if (state.startsWith("Starting")) return "启动中";
    return state;
}

static QString formatUptime(qint64 totalSecs)
{
    if (totalSecs <= 0) return "--";
    int days = totalSecs / 86400;
    int hours = (totalSecs % 86400) / 3600;
    int mins = (totalSecs % 3600) / 60;
    int secs = totalSecs % 60;
    QString t = QString("%1:%2:%3")
                .arg(hours, 2, 10, QChar('0'))
                .arg(mins, 2, 10, QChar('0'))
                .arg(secs, 2, 10, QChar('0'));
    if (days > 0) t = QString("%1天 ").arg(days) + t;
    return t;
}

VirtualMachinePage::VirtualMachinePage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("虚拟机列表");

    QWidget *leftPanel = new QWidget(this);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    QHBoxLayout *searchLayout = new QHBoxLayout();
    ElaLineEdit *searchEdit = new ElaLineEdit(this);
    searchEdit->setPlaceholderText("搜索虚拟机...");
    searchEdit->setFixedHeight(33);

    ElaToolButton *refreshBtn = new ElaToolButton(this);
    refreshBtn->setElaIcon(ElaIconType::ArrowRotateRight);
    refreshBtn->setFixedSize(33, 33);
    connect(refreshBtn, &ElaToolButton::clicked, this, [=]()
    {
        HyperVManager::getInstance()->refreshVMList();
    });

    searchLayout->addWidget(searchEdit);
    searchLayout->addWidget(refreshBtn);
    leftLayout->addLayout(searchLayout);

    _vmListModel = new QStandardItemModel(this);
    _vmListView = new ElaListView(this);
    _vmListView->setModel(_vmListModel);
    _vmListView->setItemHeight(42);
    _vmListView->setIsTransparent(true);
    _vmListView->setContextMenuPolicy(Qt::CustomContextMenu);
    _vmListView->setMinimumWidth(180);

    connect(_vmListView, &QListView::customContextMenuRequested, this, &VirtualMachinePage::showContextMenu);
    connect(_vmListView, &QListView::doubleClicked, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->connectVM(name);
    });

    leftLayout->addWidget(_vmListView);

    connect(searchEdit, &ElaLineEdit::textChanged, this, [this](const QString& text)
    {
        for (int i = 0; i < _vmListModel->rowCount(); ++i)
        {
            bool match = _vmListModel->item(i)->text().contains(text, Qt::CaseInsensitive);
            _vmListView->setRowHidden(i, !match);
        }
    });

    _contextMenu = new ElaMenu(this);
    _contextMenu->setMenuItemHeight(27);
    connect(_contextMenu->addElaIconAction(ElaIconType::Play, "启动"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->startVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::Stop, "关闭"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->stopVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::Pause, "暂停"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->pauseVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::CirclePlay, "恢复"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->resumeVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::ArrowRotateRight, "重启"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->restartVM(name);
    });
    _contextMenu->addSeparator();
    connect(_contextMenu->addElaIconAction(ElaIconType::FloppyDisk, "保存"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->saveVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::Desktop, "连接"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty()) HyperVManager::getInstance()->connectVM(name);
    });
    connect(_contextMenu->addElaIconAction(ElaIconType::GearComplex, "设置"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (!name.isEmpty())
        {
            VMSettingsDialog *dlg = new VMSettingsDialog(name);
            dlg->show();
        }
    });
    _contextMenu->addSeparator();
    connect(_contextMenu->addElaIconAction(ElaIconType::TrashCan, "删除"), &QAction::triggered, this, [=]()
    {
        QString name = selectedVMName();
        if (name.isEmpty()) return;
        ElaContentDialog *dialog = new ElaContentDialog(this);
        dialog->setLeftButtonText("取消");
        dialog->setMiddleButtonText("");
        dialog->setRightButtonText("确认删除");
        dialog->setCentralWidget(new ElaText(QString("确定要删除虚拟机 \"%1\" 吗？").arg(name), this));
        connect(dialog, &ElaContentDialog::rightButtonClicked, this, [=]()
        {
            HyperVManager::getInstance()->deleteVM(name);
        });
        dialog->exec();
    });

    _emptyHint = new QWidget(this);
    QVBoxLayout *emptyLayout = new QVBoxLayout(_emptyHint);
    ElaText *emptyText = new ElaText("选择一个虚拟机查看详情", this);
    emptyText->setTextPixelSize(16);
    emptyText->setMinimumWidth(200);
    emptyText->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch();
    emptyLayout->addWidget(emptyText, 0, Qt::AlignCenter);
    emptyLayout->addStretch();

    _detailPanel = new QWidget(this);
    QVBoxLayout *detailOuterLayout = new QVBoxLayout(_detailPanel);
    detailOuterLayout->setContentsMargins(0, 0, 0, 0);

    ElaScrollArea *detailScroll = new ElaScrollArea(this);
    detailScroll->setWidgetResizable(true);
    QWidget *detailInner = new QWidget(this);
    QVBoxLayout *detailLayout = new QVBoxLayout(detailInner);
    detailLayout->setContentsMargins(8, 0, 0, 0);
    detailLayout->setSpacing(10);
    detailScroll->setWidget(detailInner);
    detailOuterLayout->addWidget(detailScroll);

    QWidget *detailCard = new QWidget(this);
    QHBoxLayout *cardLayout = new QHBoxLayout(detailCard);
    cardLayout->setContentsMargins(0, 0, 0, 0);
    cardLayout->setSpacing(24);

    _thumbnailLabel = new QLabel(this);
    _thumbnailLabel->setFixedSize(160, 120);
    _thumbnailLabel->setAlignment(Qt::AlignCenter);
    _thumbnailLabel->setStyleSheet(
        "QLabel { background-color: #1a1a2e; border: 1px solid #444; border-radius: 6px; color: #666; font-size: 12px; }");
    _thumbnailLabel->setText("预览");
    cardLayout->addWidget(_thumbnailLabel, 0, Qt::AlignTop);

    auto makeInfoText = [this](const QString& text, int pixelSize) -> ElaText*
    {
        ElaText *t = new ElaText(text, pixelSize, this);
        t->setWordWrap(false);
        t->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        t->setMinimumHeight(pixelSize + 12);
        return t;
    };

    _vmNameLabel = makeInfoText("--", 22);
    _stateLabel = makeInfoText("状态: --", 14);
    _uptimeLabel = makeInfoText("运行时间: --", 14);
    _cpuInfoLabel = makeInfoText("CPU: --", 14);
    _memoryInfoLabel = makeInfoText("内存: --", 14);

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(4);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    infoLayout->addWidget(_vmNameLabel);
    infoLayout->addSpacing(4);

    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(24);
    row1->addWidget(_stateLabel);
    row1->addWidget(_uptimeLabel);
    row1->addStretch();
    infoLayout->addLayout(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    row2->setSpacing(24);
    row2->addWidget(_cpuInfoLabel);
    row2->addWidget(_memoryInfoLabel);
    row2->addStretch();
    infoLayout->addLayout(row2);

    infoLayout->addSpacing(8);

    QHBoxLayout *ctrlRow = new QHBoxLayout();
    ctrlRow->setSpacing(6);

    auto makeCtrlBtn = [this](ElaIconType::IconName icon, const QString& tip) -> ElaToolButton*
    {
        ElaToolButton *btn = new ElaToolButton(this);
        btn->setElaIcon(icon);
        btn->setToolTip(tip);
        btn->setFixedSize(36, 36);
        return btn;
    };

    _startBtn = makeCtrlBtn(ElaIconType::Play, "启动");
    connect(_startBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->startVM(_selectedVmName);
    });

    _stopBtn = makeCtrlBtn(ElaIconType::Stop, "关闭");
    connect(_stopBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->stopVM(_selectedVmName);
    });

    _turnOffBtn = makeCtrlBtn(ElaIconType::PowerOff, "强制关机");
    connect(_turnOffBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->turnOffVM(_selectedVmName);
    });

    _pauseBtn = makeCtrlBtn(ElaIconType::Pause, "暂停");
    connect(_pauseBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->pauseVM(_selectedVmName);
    });

    _restartBtn = makeCtrlBtn(ElaIconType::ArrowRotateRight, "重启");
    connect(_restartBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->restartVM(_selectedVmName);
    });

    _connectBtn = makeCtrlBtn(ElaIconType::Desktop, "连接");
    connect(_connectBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty()) HyperVManager::getInstance()->connectVM(_selectedVmName);
    });

    _settingsBtn = makeCtrlBtn(ElaIconType::GearComplex, "设置");
    connect(_settingsBtn, &ElaToolButton::clicked, this, [=]()
    {
        if (!_selectedVmName.isEmpty())
        {
            VMSettingsDialog *dlg = new VMSettingsDialog(_selectedVmName);
            dlg->show();
        }
    });

    ctrlRow->addWidget(_startBtn);
    ctrlRow->addWidget(_stopBtn);
    ctrlRow->addWidget(_turnOffBtn);
    ctrlRow->addWidget(_pauseBtn);
    ctrlRow->addWidget(_restartBtn);
    ctrlRow->addWidget(_connectBtn);
    ctrlRow->addWidget(_settingsBtn);
    ctrlRow->addStretch();
    infoLayout->addLayout(ctrlRow);

    cardLayout->addLayout(infoLayout, 1);

    detailLayout->addWidget(detailCard);

    auto makeDrawerSection = [this](const QString& title, const QString& subtitle,
                                    ElaIconType::IconName icon, int valueSize,
                                    ElaText *& valueLabel, QWidget *& content, QVBoxLayout *& contentLayout)
        -> ElaDrawerArea*
    {
        ElaDrawerArea *drawer = new ElaDrawerArea(this);

        QWidget *headerWidget = new QWidget(this);
        QHBoxLayout *headerLayout = new QHBoxLayout(headerWidget);
        headerLayout->setContentsMargins(4, 0, 8, 0);

        ElaToolButton *iconBtn = new ElaToolButton(this);
        iconBtn->setElaIcon(icon);
        iconBtn->setFixedSize(28, 28);
        iconBtn->setEnabled(false);
        headerLayout->addWidget(iconBtn);
        headerLayout->addSpacing(8);

        QVBoxLayout *titleLayout = new QVBoxLayout();
        titleLayout->setSpacing(1);
        ElaText *titleLabel = new ElaText(title, 15, this);
        titleLabel->setWordWrap(false);
        titleLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        titleLabel->setMinimumHeight(22);
        titleLayout->addWidget(titleLabel);
        if (!subtitle.isEmpty())
        {
            ElaText *subLabel = new ElaText(subtitle, 11, this);
            subLabel->setWordWrap(false);
            subLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            subLabel->setMinimumHeight(16);
            subLabel->setStyleSheet("color: #888;");
            titleLayout->addWidget(subLabel);
        }
        headerLayout->addLayout(titleLayout, 1);

        valueLabel = new ElaText("--", valueSize, this);
        valueLabel->setWordWrap(false);
        valueLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        valueLabel->setMinimumHeight(valueSize + 10);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        headerLayout->addWidget(valueLabel);

        for (QObject *child : headerWidget->findChildren<QObject*>())
        {
            if (auto *w = qobject_cast<QWidget*>(child))
                w->setObjectName(QString());
        }

        drawer->setDrawerHeader(headerWidget);
        drawer->setHeaderHeight(subtitle.isEmpty() ? 46 : 58);

        content = new QWidget(this);
        contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(16, 8, 16, 12);
        contentLayout->setSpacing(6);
        drawer->addDrawer(content);

        return drawer;
    };

    ElaDrawerArea *cpuDrawer = makeDrawerSection(
        "处理器", "处理器使用情况", ElaIconType::Microchip, 20,
        _metricCpuLabel, _cpuContent, _cpuContentLayout);

    ElaDrawerArea *memDrawer = makeDrawerSection(
        "内存", "内存使用监控", ElaIconType::MemoPad, 20,
        _metricMemLabel, _memContent, _memContentLayout);

    ElaDrawerArea *storageDrawer = makeDrawerSection(
        "存储", "虚拟磁盘信息", ElaIconType::HardDrive, 14,
        _metricStorageLabel, _storageContent, _storageContentLayout);

    ElaDrawerArea *netDrawer = makeDrawerSection(
        "网络适配器", "网络连接信息", ElaIconType::Ethernet, 14,
        _metricNetLabel, _netContent, _netContentLayout);

    ElaDrawerArea *gpuDrawer = makeDrawerSection(
        "显卡", "GPU 分区信息", ElaIconType::Desktop, 14,
        _metricGpuLabel, _gpuContent, _gpuContentLayout);

    detailLayout->addWidget(cpuDrawer);
    detailLayout->addWidget(memDrawer);
    detailLayout->addWidget(storageDrawer);
    detailLayout->addWidget(netDrawer);
    detailLayout->addWidget(gpuDrawer);
    detailLayout->addStretch();

    QStackedWidget *rightStack = new QStackedWidget(this);
    rightStack->addWidget(_emptyHint);
    rightStack->addWidget(_detailPanel);
    rightStack->setCurrentWidget(_emptyHint);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setStyleSheet("QSplitter, QSplitter::handle, QStackedWidget, QWidget { background: transparent; }");
    splitter->addWidget(leftPanel);
    splitter->addWidget(rightStack);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 600});
    splitter->setHandleWidth(8);
    splitter->setChildrenCollapsible(false);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("虚拟机");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addWidget(splitter);
    addCentralWidget(centralWidget, true, true, 0);

    _refreshTimer = new QTimer(this);
    _refreshTimer->setInterval(1000);
    connect(_refreshTimer, &QTimer::timeout, this, &VirtualMachinePage::refreshPreview);

    connect(HyperVManager::getInstance(), &HyperVManager::vmListRefreshed,
            this, &VirtualMachinePage::refreshVmList);
    connect(HyperVManager::getInstance(), &HyperVManager::operationFinished, this,
            [=](const QString& vmName, const QString& operation, bool success, const QString& message)
            {
                if (success)
                {
                    ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功",
                                           QString("虚拟机 %1 %2成功").arg(vmName, operation), 2000);
                    HyperVManager::getInstance()->refreshVMList();
                }
                else
                {
                    ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                         QString("虚拟机 %1 %2失败: %3").arg(vmName, operation, message), 5000);
                }
            });

    auto *stack = rightStack;
    connect(_vmListView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [stack, this](const QModelIndex& current)
            {
                stack->setCurrentWidget(current.isValid() ? _detailPanel : _emptyHint);
                onVmSelectionChanged();
            });

    refreshVmList();
}

VirtualMachinePage::~VirtualMachinePage()
{
}

static void clearLayout(QLayout *layout)
{
    while (QLayoutItem *item = layout->takeAt(0))
    {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
}

void VirtualMachinePage::refreshVmList()
{
    QString prevSelected = _selectedVmName;
    _vmListModel->clear();

    auto vmList = HyperVManager::getInstance()->getVMList();
    int restoreRow = -1;
    for (int i = 0; i < vmList.size(); ++i)
    {
        const auto& vm = vmList[i];
        QStandardItem *item = new QStandardItem();

        QString displayText = vm.name;
        item->setText(displayText);
        item->setData(vm.name, Qt::UserRole);
        item->setData(vm.state, Qt::UserRole + 1);

        QPixmap dot(10, 10);
        dot.fill(Qt::transparent);
        QPainter p(&dot);
        p.setRenderHint(QPainter::Antialiasing);
        p.setBrush(stateColor(vm.state));
        p.setPen(Qt::NoPen);
        p.drawEllipse(1, 1, 8, 8);
        p.end();
        item->setIcon(QIcon(dot));

        _vmListModel->appendRow(item);

        if (vm.name == prevSelected)
            restoreRow = i;
    }

    if (restoreRow >= 0)
        _vmListView->setCurrentIndex(_vmListModel->index(restoreRow, 0));
}

void VirtualMachinePage::onVmSelectionChanged()
{
    QModelIndex idx = _vmListView->currentIndex();
    if (!idx.isValid())
    {
        _selectedVmName.clear();
        _refreshTimer->stop();
        return;
    }

    _selectedVmName = _vmListModel->itemFromIndex(idx)->data(Qt::UserRole).toString();
    _baseUptimeSec = 0;
    _baseUptimeTimestamp = 0;
    QString state = _vmListModel->itemFromIndex(idx)->data(Qt::UserRole + 1).toString();

    _vmNameLabel->setText(_selectedVmName);
    _stateLabel->setText(QString("状态: %1").arg(stateTextCn(state)));

    _startBtn->setVisible(state != "Running");
    _stopBtn->setVisible(state == "Running" || state == "Paused");
    _turnOffBtn->setVisible(state == "Running" || state == "Paused");
    _pauseBtn->setVisible(state == "Running");
    _restartBtn->setVisible(state == "Running");

    auto vmList = HyperVManager::getInstance()->getVMList();
    for (const auto& vm : vmList)
    {
        if (vm.name == _selectedVmName)
        {
            if (vm.state == "Running")
                _uptimeLabel->setText(QString("运行时间: %1").arg(formatUptime(vm.uptimeSeconds)));
            else
                _uptimeLabel->setText("运行时间: --");
            _cpuInfoLabel->setText(QString("CPU: %1 核").arg(vm.cpuCount));
            qint64 mem = (vm.memoryMB > 0) ? vm.memoryMB : vm.memoryStartupMB;
            _memoryInfoLabel->setText(QString("内存: %1 MB").arg(mem));
            break;
        }
    }

    _thumbnailLabel->setPixmap(QPixmap());
    _thumbnailLabel->setText(state == "Running" ? "加载中..." : "虚拟机未运行");
    _metricCpuLabel->setText("--");
    _metricMemLabel->setText("--");
    _metricStorageLabel->setText("--");
    _metricNetLabel->setText("--");
    _metricGpuLabel->setText("--");

    refreshDeviceInfo();

    _thumbnailPending = false;
    _summaryPending = false;
    refreshPreview();
    _refreshTimer->start();
}

void VirtualMachinePage::refreshPreview()
{
    if (_selectedVmName.isEmpty())
        return;

    const QString vmName = _selectedVmName;

    QString vmState;
    auto vmList = HyperVManager::getInstance()->getVMList();
    for (const auto& vm : vmList)
    {
        if (vm.name == vmName)
        {
            vmState = vm.state;
            break;
        }
    }

    const bool isRunning = (vmState == "Running");
    const bool isOff = (vmState == "Off");
    const bool isPaused = (vmState == "Paused");
    _startBtn->setVisible(!isRunning);
    _stopBtn->setVisible(isRunning || isPaused);
    _turnOffBtn->setVisible(isRunning || isPaused);
    _pauseBtn->setVisible(isRunning);
    _restartBtn->setVisible(isRunning);

    if (vmState == "Running" && !_thumbnailPending)
    {
        _thumbnailPending = true;
        QPointer<VirtualMachinePage> guard(this);
        HyperVManager::getInstance()->getVMThumbnailAsync(vmName, THUMBNAIL_W, THUMBNAIL_H, [guard, vmName](const QImage& image, const QString&)
        {
            if (!guard) return;
            guard->_thumbnailPending = false;
            if (guard->_selectedVmName != vmName) return;

            if (!image.isNull())
            {
                guard->_thumbnailLabel->setText("");
                guard->_thumbnailLabel->setPixmap(
                    QPixmap::fromImage(image).scaled(
                        guard->_thumbnailLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            }
            else
            {
                guard->_thumbnailLabel->setPixmap(QPixmap());
                guard->_thumbnailLabel->setText("无法获取屏幕");
            }
        });
    }
    else if (vmState != "Running")
    {
        _thumbnailLabel->setPixmap(QPixmap());
        _thumbnailLabel->setText("虚拟机未运行");
    }

    if (_baseUptimeTimestamp > 0 && vmState == "Running")
    {
        qint64 elapsed = (QDateTime::currentMSecsSinceEpoch() - _baseUptimeTimestamp) / 1000;
        _uptimeLabel->setText(QString("运行时间: %1").arg(formatUptime(_baseUptimeSec + elapsed)));
    }
    else if (vmState != "Running")
    {
        _uptimeLabel->setText("运行时间: --");
    }

    if (!_summaryPending)
    {
        _summaryPending = true;
        QPointer<VirtualMachinePage> guard(this);
        HyperVManager::getInstance()->getVMSummaryInfoAsync(vmName, [guard, vmName](const VMSummaryInfo& info, const QString& error)
        {
            if (!guard) return;
            guard->_summaryPending = false;
            if (guard->_selectedVmName != vmName) return;

            if (!error.isEmpty())
                return;

            guard->_stateLabel->setText(QString("状态: %1").arg(stateTextCn(info.state)));

            bool isRunning = (info.state == "Running");

            if (isRunning)
            {
                guard->_cpuInfoLabel->setText(QString("CPU: %1%").arg(info.cpuUsage));
                guard->_metricCpuLabel->setText(QString("%1%").arg(info.cpuUsage));
            }
            else
            {
                int cpuCount = 0;
                auto vmList = HyperVManager::getInstance()->getVMList();
                for (const auto& vm : vmList)
                {
                    if (vm.name == guard->_selectedVmName)
                    {
                        cpuCount = vm.cpuCount;
                        break;
                    }
                }
                guard->_cpuInfoLabel->setText(QString("CPU: %1 核").arg(cpuCount));
                guard->_metricCpuLabel->setText(QString("%1 核").arg(cpuCount));
            }

            {
                qint64 serverSec = info.uptimeMs / 1000;
                qint64 now = QDateTime::currentMSecsSinceEpoch();
                if (guard->_baseUptimeTimestamp == 0)
                {
                    guard->_baseUptimeSec = serverSec;
                    guard->_baseUptimeTimestamp = now;
                }
                else
                {
                    qint64 localSec = guard->_baseUptimeSec + (now - guard->_baseUptimeTimestamp) / 1000;
                    if (qAbs(localSec - serverSec) > 5)
                    {
                        guard->_baseUptimeSec = serverSec;
                        guard->_baseUptimeTimestamp = now;
                    }
                }
            }

            guard->_memoryInfoLabel->setText(QString("内存: %1 MB").arg(info.memoryUsageMB));
            double usedGB = info.memoryUsageMB / 1024.0;
            guard->_metricMemLabel->setText(
                QString("%1 GB").arg(usedGB, 0, 'f', 1));
        });
    }
}

void VirtualMachinePage::refreshDeviceInfo()
{
    if (_selectedVmName.isEmpty())
        return;

    const QString vmName = _selectedVmName;
    QPointer<VirtualMachinePage> guard(this);

    auto makeDetailText = [this](const QString& text, int pixelSize = 13) -> ElaText*
    {
        ElaText *t = new ElaText(text, pixelSize, this);
        t->setWordWrap(false);
        t->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        t->setMinimumHeight(pixelSize + 10);
        return t;
    };

    HyperVManager::getInstance()->getVMProcessorAdvancedAsync(vmName, [guard, vmName, makeDetailText](const VMProcessorAdvancedSettings& s, const QString& error)
    {
        if (!guard || guard->_selectedVmName != vmName) return;
        clearLayout(guard->_cpuContentLayout);
        if (!error.isEmpty())
        {
            guard->_cpuContentLayout->addWidget(makeDetailText("获取失败: " + error));
            return;
        }
        guard->_cpuContentLayout->addWidget(makeDetailText(QString("处理器数量: %1").arg(s.count)));
        guard->_cpuContentLayout->addWidget(makeDetailText(QString("预留: %1%  限制: %2%  权重: %3").arg(s.reserve).arg(s.limit).arg(s.weight)));
        guard->_cpuContentLayout->addWidget(makeDetailText(QString("嵌套虚拟化: %1").arg(s.exposeVirtualizationExtensions ? "已启用" : "未启用")));
        if (s.hideHypervisor)
            guard->_cpuContentLayout->addWidget(makeDetailText("隐藏虚拟机监控程序: 是"));
    });

    HyperVManager::getInstance()->getVMMemoryAdvancedAsync(vmName, [guard, vmName, makeDetailText](const VMMemoryAdvancedSettings& s, const QString& error)
    {
        if (!guard || guard->_selectedVmName != vmName) return;
        clearLayout(guard->_memContentLayout);
        if (!error.isEmpty())
        {
            guard->_memContentLayout->addWidget(makeDetailText("获取失败: " + error));
            return;
        }
        guard->_memContentLayout->addWidget(makeDetailText(QString("启动内存: %1 MB").arg(s.startupMB)));
        guard->_memContentLayout->addWidget(makeDetailText(QString("动态内存: %1").arg(s.dynamicMemoryEnabled ? "已启用" : "未启用")));
        if (s.dynamicMemoryEnabled)
        {
            guard->_memContentLayout->addWidget(makeDetailText(QString("最小: %1 MB  最大: %2 MB").arg(s.minimumMB).arg(s.maximumMB)));
            guard->_memContentLayout->addWidget(makeDetailText(QString("缓冲区: %1%  权重: %2").arg(s.buffer).arg(s.weight)));
        }
    });

    HyperVManager::getInstance()->getVMStorageAsync(vmName, [guard, vmName, makeDetailText](const QList<VMStorageInfo>& list, const QString& error)
    {
        if (!guard || guard->_selectedVmName != vmName) return;
        clearLayout(guard->_storageContentLayout);

        if (!error.isEmpty())
        {
            guard->_metricStorageLabel->setText("获取失败");
            guard->_storageContentLayout->addWidget(makeDetailText("获取失败: " + error));
            return;
        }

        int diskCount = 0;
        qint64 totalSizeMB = 0;
        for (const auto& s : list)
        {
            if (s.diskType == "VirtualHardDisk")
            {
                diskCount++;
                totalSizeMB += s.maxSizeMB;
            }
        }
        if (diskCount == 0)
            guard->_metricStorageLabel->setText("无磁盘");
        else
            guard->_metricStorageLabel->setText(
                QString("%1 个磁盘, %2 GB").arg(diskCount).arg(totalSizeMB / 1024.0, 0, 'f', 1));

        if (list.isEmpty())
        {
            guard->_storageContentLayout->addWidget(makeDetailText("无虚拟磁盘"));
        }
        else
        {
            for (const auto& s : list)
            {
                QString typeName = (s.diskType == "VirtualHardDisk") ? "VHD" : s.diskType;
                QString sizeText;
                if (s.maxSizeMB > 0)
                    sizeText = QString("  %1 / %2 GB").arg(s.currentSizeMB / 1024.0, 0, 'f', 1).arg(s.maxSizeMB / 1024.0, 0, 'f', 1);

                ElaText *pathLabel = makeDetailText(QString("[%1] %2%3").arg(typeName, s.path, sizeText), 12);
                guard->_storageContentLayout->addWidget(pathLabel);
            }
        }
    });

    HyperVManager::getInstance()->getVMNetworkAdaptersAsync(vmName, [guard, vmName, makeDetailText](const QList<VMNetworkAdapterInfo>& list, const QString& error)
    {
        if (!guard || guard->_selectedVmName != vmName) return;
        clearLayout(guard->_netContentLayout);

        if (!error.isEmpty())
        {
            guard->_metricNetLabel->setText("获取失败");
            guard->_netContentLayout->addWidget(makeDetailText("获取失败: " + error));
            return;
        }

        if (list.isEmpty())
        {
            guard->_metricNetLabel->setText("无网络适配器");
            guard->_netContentLayout->addWidget(makeDetailText("无网络适配器"));
        }
        else
        {
            QStringList summaryParts;
            for (const auto& a : list)
            {
                QString entry = a.switchName.isEmpty() ? "未连接" : a.switchName;
                if (!a.ipAddresses.isEmpty())
                {
                    for (const QString& ip : a.ipAddresses.split(","))
                    {
                        QString trimmed = ip.trimmed();
                        if (!trimmed.contains(':') && !trimmed.isEmpty())
                        {
                            entry += " (" + trimmed + ")";
                            break;
                        }
                    }
                }
                summaryParts << entry;
            }
            guard->_metricNetLabel->setText(summaryParts.join(", "));

            for (const auto& a : list)
            {
                QString switchText = a.switchName.isEmpty() ? "未连接" : a.switchName;
                guard->_netContentLayout->addWidget(
                    makeDetailText(QString("%1 - %2").arg(a.adapterName, switchText), 13));

                if (!a.ipAddresses.isEmpty())
                {
                    for (const QString& ip : a.ipAddresses.split(","))
                    {
                        QString trimmed = ip.trimmed();
                        if (!trimmed.contains(':') && !trimmed.isEmpty())
                        {
                            guard->_netContentLayout->addWidget(
                                makeDetailText(QString("  IP: %1").arg(trimmed), 12));
                        }
                    }
                }

                QString mac = a.macAddress;
                if (mac.length() == 12 && !mac.contains(':'))
                {
                    QString formatted;
                    for (int i = 0; i < 12; i += 2)
                    {
                        if (!formatted.isEmpty()) formatted += ':';
                        formatted += mac.mid(i, 2);
                    }
                    mac = formatted;
                }
                guard->_netContentLayout->addWidget(
                    makeDetailText(QString("  MAC: %1%2")
                                   .arg(mac, a.dynamicMacAddress ? " (动态)" : " (静态)"), 12));

                if (a.vlanId > 0)
                    guard->_netContentLayout->addWidget(
                        makeDetailText(QString("  VLAN: %1").arg(a.vlanId), 12));
            }
        }
    });

    HyperVManager::getInstance()->getVMGpuPartitionStatusAsync(vmName, [guard, vmName, makeDetailText](const VMGpuPartitionStatus& status, const QString& error)
    {
        if (!guard || guard->_selectedVmName != vmName) return;
        clearLayout(guard->_gpuContentLayout);

        if (!error.isEmpty())
        {
            guard->_metricGpuLabel->setText("获取失败");
            guard->_gpuContentLayout->addWidget(makeDetailText("获取失败: " + error));
            return;
        }

        if (status.enabled)
        {
            guard->_metricGpuLabel->setText(QString("GPU-P %1%").arg(status.allocationPercent));
            guard->_gpuContentLayout->addWidget(
                makeDetailText(QString("GPU 分区: 已启用"), 13));
            guard->_gpuContentLayout->addWidget(
                makeDetailText(QString("分配比例: %1%").arg(status.allocationPercent), 12));
            if (!status.instancePath.isEmpty())
                guard->_gpuContentLayout->addWidget(
                    makeDetailText(QString("实例路径: %1").arg(status.instancePath), 12));
        }
        else
        {
            guard->_metricGpuLabel->setText("未配置");
            guard->_gpuContentLayout->addWidget(makeDetailText("未配置 GPU 分区"));
        }
    });
}

void VirtualMachinePage::showContextMenu(const QPoint& pos)
{
    if (_vmListView->indexAt(pos).isValid())
    {
        _contextMenu->popup(_vmListView->viewport()->mapToGlobal(pos));
    }
}

QString VirtualMachinePage::selectedVMName() const
{
    QModelIndex idx = _vmListView->currentIndex();
    if (!idx.isValid())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个虚拟机", 2000);
        return {};
    }
    return _vmListModel->itemFromIndex(idx)->data(Qt::UserRole).toString();
}

#include "USBPage.h"
#include "UsbTunnelManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "ElaScrollPageArea.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToolButton.h"

USBPage::USBPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("USB 设备");

    ElaScrollPageArea *toolArea = new ElaScrollPageArea(this);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolArea);

    ElaToolButton *refreshBtn = new ElaToolButton(this);
    refreshBtn->setElaIcon(ElaIconType::ArrowRotateRight);
    refreshBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    refreshBtn->setText("刷新");
    connect(refreshBtn, &ElaToolButton::clicked, this, &USBPage::refreshDevices);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addStretch();

    _model = new QStandardItemModel(this);
    _model->setHorizontalHeaderLabels({"设备名称", "VID:PID", "类型", "状态"});

    _table = new ElaTableView(this);
    _table->setModel(_model);
    _table->setSelectionBehavior(QAbstractItemView::SelectRows);
    _table->setSelectionMode(QAbstractItemView::SingleSelection);
    _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _table->setAlternatingRowColors(true);
    _table->horizontalHeader()->setStretchLastSection(true);
    _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _table->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    _table->verticalHeader()->setVisible(false);
    _table->setMinimumHeight(300);

    _statusText = new ElaText("正在检测 USB 设备...", this);
    _statusText->setWordWrap(true);
    _statusText->setTextPixelSize(13);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("USB 设备");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(toolArea);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(_table);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(_statusText);
    centerLayout->addStretch();

    addCentralWidget(centralWidget, true, true, 0);

    auto mgr = UsbTunnelManager::getInstance();
    connect(mgr, &UsbTunnelManager::devicesRefreshed, this, &USBPage::onDevicesRefreshed);

    refreshDevices();
}

USBPage::~USBPage()
{
}

void USBPage::refreshDevices()
{
    _statusText->setText("正在检测 USB 设备...");
    UsbTunnelManager::getInstance()->refreshDevices();
}

void USBPage::onDevicesRefreshed()
{
    const auto devices = UsbTunnelManager::getInstance()->devices();

    _model->removeRows(0, _model->rowCount());
    for (const auto& dev : devices)
    {
        auto makeItem = [](const QString& text)
        {
            QStandardItem *item = new QStandardItem(text);
            item->setTextAlignment(Qt::AlignCenter);
            return item;
        };

        QList<QStandardItem*> row;
        row << makeItem(dev.description);
        row << makeItem(dev.vidPid);
        row << makeItem(dev.deviceClass);
        row << makeItem(dev.status);
        _model->appendRow(row);
    }

    if (devices.isEmpty())
        _statusText->setText("未检测到 USB 设备。");
    else
        _statusText->setText(QString("检测到 %1 个 USB 设备。").arg(devices.size()));
}

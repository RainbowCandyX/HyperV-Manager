#include "NetworkPage.h"
#include "HyperVManager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QPointer>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaLineEdit.h"
#include "ElaMessageBar.h"
#include "ElaPushButton.h"
#include "ElaScrollPageArea.h"
#include "ElaTableView.h"
#include "ElaText.h"
#include "ElaToolButton.h"
#include "ElaContentDialog.h"

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

NetworkPage::NetworkPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("网络管理");

    ElaScrollPageArea *toolArea = new ElaScrollPageArea(this);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolArea);

    ElaToolButton *refreshBtn = new ElaToolButton(this);
    refreshBtn->setElaIcon(ElaIconType::ArrowRotateRight);
    refreshBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    refreshBtn->setText("刷新");
    connect(refreshBtn, &ElaToolButton::clicked, this, &NetworkPage::refreshSwitchTable);

    toolLayout->addWidget(refreshBtn);
    toolLayout->addStretch();

    ElaText *switchTitle = new ElaText("虚拟交换机", this);
    switchTitle->setTextPixelSize(18);

    _switchModel = new QStandardItemModel(this);
    _switchModel->setHorizontalHeaderLabels({"名称", "类型", "管理OS共享", "物理适配器"});

    _switchTable = new ElaTableView(this);
    _switchTable->setModel(_switchModel);
    _switchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _switchTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _switchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _switchTable->setAlternatingRowColors(true);
    _switchTable->horizontalHeader()->setStretchLastSection(true);
    _switchTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    _switchTable->horizontalHeader()->setDefaultAlignment(Qt::AlignCenter);
    _switchTable->verticalHeader()->setVisible(false);
    _switchTable->setMinimumHeight(200);

    ElaPushButton *deleteBtn = new ElaPushButton("删除选中交换机", this);
    deleteBtn->setElaIcon(ElaIconType::TrashCan, 14);
    deleteBtn->setFixedSize(160, 36);
    connect(deleteBtn, &ElaPushButton::clicked, this, &NetworkPage::onDeleteSwitch);

    ElaText *createTitle = new ElaText("创建虚拟交换机", this);
    createTitle->setTextPixelSize(18);

    _newSwitchName = new ElaLineEdit(this);
    _newSwitchName->setPlaceholderText("输入交换机名称");
    _newSwitchName->setFixedWidth(250);

    _switchTypeCombo = new ElaComboBox(this);
    _switchTypeCombo->addItem("外部 (External)", "External");
    _switchTypeCombo->addItem("内部 (Internal)", "Internal");
    _switchTypeCombo->addItem("专用 (Private)", "Private");

    _adapterCombo = new ElaComboBox(this);
    _adapterCombo->addItem("加载中...");
    _adapterCombo->setEnabled(false);
    _adapterCombo->setFixedWidth(300);

    connect(_switchTypeCombo, &ElaComboBox::currentIndexChanged, this, [this](int index)
    {
        _adapterCombo->setEnabled(index == 0 && _adaptersReady);
    });

    ElaPushButton *createBtn = new ElaPushButton("创建交换机", this);
    createBtn->setElaIcon(ElaIconType::CirclePlus, 14);
    createBtn->setFixedSize(140, 36);
    connect(createBtn, &ElaPushButton::clicked, this, &NetworkPage::onCreateSwitch);

    QWidget *centralWidget = new QWidget(this);
    centralWidget->setWindowTitle("网络管理");
    QVBoxLayout *centerLayout = new QVBoxLayout(centralWidget);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(toolArea);
    centerLayout->addSpacing(10);
    centerLayout->addWidget(switchTitle);
    centerLayout->addSpacing(5);
    centerLayout->addWidget(_switchTable);
    centerLayout->addSpacing(5);

    QHBoxLayout *delLayout = new QHBoxLayout();
    delLayout->addStretch();
    delLayout->addWidget(deleteBtn);
    centerLayout->addLayout(delLayout);

    centerLayout->addSpacing(20);
    centerLayout->addWidget(createTitle);
    centerLayout->addSpacing(5);
    centerLayout->addWidget(createFormRow("交换机名称", _newSwitchName, this));
    centerLayout->addWidget(createFormRow("交换机类型", _switchTypeCombo, this));
    centerLayout->addWidget(createFormRow("物理适配器", _adapterCombo, this));
    centerLayout->addSpacing(10);

    QHBoxLayout *createLayout = new QHBoxLayout();
    createLayout->addStretch();
    createLayout->addWidget(createBtn);
    createLayout->addStretch();
    centerLayout->addLayout(createLayout);
    centerLayout->addStretch();

    addCentralWidget(centralWidget, true, true, 0);

    connect(HyperVManager::getInstance(), &HyperVManager::operationFinished, this,
            [this](const QString&, const QString& operation, bool success, const QString& message)
            {
                if (operation.contains("交换机"))
                {
                    if (success)
                    {
                        ElaMessageBar::success(ElaMessageBarType::BottomRight, "成功", operation + "成功", 2000);
                        refreshSwitchTable();
                    }
                    else
                    {
                        ElaMessageBar::error(ElaMessageBarType::BottomRight, "失败",
                                             operation + "失败: " + message, 5000);
                    }
                }
            });

    refreshSwitchTable();

    QPointer<NetworkPage> guard(this);
    HyperVManager::getInstance()->getPhysicalAdaptersAsync([guard](const QList<PhysicalAdapterInfo>& adapters, const QString& error)
    {
        if (!guard) return;
        guard->_adaptersReady = true;
        guard->_adapterCombo->clear();
        if (adapters.isEmpty())
        {
            guard->_adapterCombo->addItem("无可用物理适配器");
        }
        else
        {
            for (const auto& a : adapters)
            {
                guard->_adapterCombo->addItem(
                    QString("%1 (%2)").arg(a.name, a.interfaceDescription),
                    a.interfaceDescription);
            }
        }
        guard->_adapterCombo->setEnabled(guard->_switchTypeCombo->currentIndex() == 0);
        if (!error.isEmpty())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "获取物理适配器失败：" + error, 3000);
        }
    });
}

NetworkPage::~NetworkPage()
{
}

void NetworkPage::refreshSwitchTable()
{
    QPointer<NetworkPage> guard(this);
    HyperVManager::getInstance()->getSwitchesDetailedAsync([guard](const QList<SwitchInfo>& switches, const QString& error)
    {
        if (!guard) return;
        guard->_switchModel->removeRows(0, guard->_switchModel->rowCount());
        for (const auto& sw : switches)
        {
            auto makeItem = [](const QString& text)
            {
                QStandardItem *item = new QStandardItem(text);
                item->setTextAlignment(Qt::AlignCenter);
                return item;
            };
            QList<QStandardItem*> row;
            row << makeItem(sw.name);

            QString typeDisplay = sw.switchType;
            if (typeDisplay == "External") typeDisplay = "外部";
            else if (typeDisplay == "Internal") typeDisplay = "内部";
            else if (typeDisplay == "Private") typeDisplay = "专用";
            row << makeItem(typeDisplay);

            row << makeItem(sw.allowManagementOS ? "是" : "否");
            row << makeItem(sw.netAdapterDescription.isEmpty() ? "-" : sw.netAdapterDescription);
            guard->_switchModel->appendRow(row);
        }
        if (!error.isEmpty())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示",
                                   "获取交换机列表失败：" + error, 3000);
        }
    });
}

void NetworkPage::onCreateSwitch()
{
    const QString name = _newSwitchName->text().trimmed();
    if (name.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请输入交换机名称", 2500);
        return;
    }

    const QString type = _switchTypeCombo->currentData().toString();
    QString adapterDesc;
    if (type == "External")
    {
        if (!_adaptersReady || _adapterCombo->count() == 0)
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请等待物理适配器加载", 2500);
            return;
        }
        adapterDesc = _adapterCombo->currentData().toString();
        if (adapterDesc.isEmpty())
        {
            ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "无可用物理适配器", 2500);
            return;
        }
    }

    HyperVManager::getInstance()->createSwitch(name, type, adapterDesc);
    ElaMessageBar::information(ElaMessageBarType::BottomRight, "信息",
                               QString("正在创建交换机 %1...").arg(name), 2000);
}

void NetworkPage::onDeleteSwitch()
{
    const QString name = selectedSwitchName();
    if (name.isEmpty())
    {
        ElaMessageBar::warning(ElaMessageBarType::BottomRight, "提示", "请先选择一个交换机", 2500);
        return;
    }

    ElaContentDialog *dialog = new ElaContentDialog(this);
    dialog->setLeftButtonText("取消");
    dialog->setMiddleButtonText("");
    dialog->setRightButtonText("确认删除");
    dialog->setCentralWidget(new ElaText(QString("确定要删除交换机 \"%1\" 吗？").arg(name), this));
    connect(dialog, &ElaContentDialog::rightButtonClicked, this, [=]()
    {
        HyperVManager::getInstance()->deleteSwitch(name);
    });
    dialog->exec();
}

QString NetworkPage::selectedSwitchName() const
{
    QModelIndex idx = _switchTable->currentIndex();
    if (!idx.isValid()) return {};
    return _switchModel->item(idx.row(), 0)->text();
}
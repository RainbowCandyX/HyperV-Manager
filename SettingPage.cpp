#include "SettingPage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QSettings>
#include <QVBoxLayout>

#include "ElaComboBox.h"
#include "ElaRadioButton.h"
#include "ElaScrollPageArea.h"
#include "ElaText.h"
#include "ElaTheme.h"
#include "ElaWindow.h"

static QSettings* appSettings()
{
    static QSettings s(QCoreApplication::applicationDirPath() + "/config.ini", QSettings::IniFormat);
    return &s;
}

SettingPage::SettingPage(QWidget *parent)
    : BasePage(parent)
{
    setWindowTitle("设置");
    initUI();
}

SettingPage::~SettingPage()
{
}

void SettingPage::initUI()
{
    _centralWidget = new QWidget(this);
    _centralWidget->setWindowTitle("设置");
    QVBoxLayout *mainLayout = new QVBoxLayout(_centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(15);


    ElaScrollPageArea *themeArea = new ElaScrollPageArea(_centralWidget);
    QHBoxLayout *themeLayout = new QHBoxLayout(themeArea);

    _themeLabel = new ElaText("主题切换", themeArea);
    _themeLabel->setTextPixelSize(15);

    _themeComboBox = new ElaComboBox(themeArea);
    _themeComboBox->addItem("日间模式");
    _themeComboBox->addItem("夜间模式");
    _themeComboBox->setCurrentIndex(eTheme->getThemeMode() == ElaThemeType::Light ? 0 : 1);
    _themeComboBox->setFixedWidth(150);

    connect(_themeComboBox, &ElaComboBox::currentIndexChanged, this, [](int index)
    {
        eTheme->setThemeMode(index == 0 ? ElaThemeType::Light : ElaThemeType::Dark);
        appSettings()->setValue("ui/theme", index);
    });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this](ElaThemeType::ThemeMode mode)
    {
        _themeComboBox->blockSignals(true);
        _themeComboBox->setCurrentIndex(mode == ElaThemeType::Light ? 0 : 1);
        _themeComboBox->blockSignals(false);
    });

    themeLayout->addWidget(_themeLabel);
    themeLayout->addStretch();
    themeLayout->addWidget(_themeComboBox);

    ElaScrollPageArea *navArea = new ElaScrollPageArea(_centralWidget);
    QHBoxLayout *navLayout = new QHBoxLayout(navArea);

    _navModeLabel = new ElaText("导航栏模式", navArea);
    _navModeLabel->setTextPixelSize(15);

    _autoButton = new ElaRadioButton("自动", navArea);
    _minimumButton = new ElaRadioButton("最小", navArea);
    _compactButton = new ElaRadioButton("紧凑", navArea);
    _maximumButton = new ElaRadioButton("完整", navArea);

    _navModeGroup = new QButtonGroup(this);
    _navModeGroup->addButton(_autoButton, 0);
    _navModeGroup->addButton(_minimumButton, 1);
    _navModeGroup->addButton(_compactButton, 2);
    _navModeGroup->addButton(_maximumButton, 3);

    int savedNavMode = appSettings()->value("ui/navMode", 0).toInt();
    auto navBtn = _navModeGroup->button(savedNavMode);
    if (navBtn)
    {
        navBtn->setChecked(true);
    }

    connect(_navModeGroup, &QButtonGroup::idToggled, this, [this](int id, bool checked)
    {
        if (checked)
        {
            ElaWindow *mainWindow = qobject_cast<ElaWindow*>(window());
            if (mainWindow)
            {
                mainWindow->setNavigationBarDisplayMode(static_cast<ElaNavigationType::NavigationDisplayMode>(id));
            }
            appSettings()->setValue("ui/navMode", id);
        }
    });

    navLayout->addWidget(_navModeLabel);
    navLayout->addStretch();
    navLayout->addWidget(_autoButton);
    navLayout->addWidget(_minimumButton);
    navLayout->addWidget(_compactButton);
    navLayout->addWidget(_maximumButton);

    mainLayout->addSpacing(30);
    mainLayout->addWidget(themeArea);
    mainLayout->addWidget(navArea);
    mainLayout->addStretch();

    addCentralWidget(_centralWidget, true, true, 0);
}


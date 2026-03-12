#ifndef SETTINGPAGE_H
#define SETTINGPAGE_H

#include "BasePage.h"

class ElaComboBox;
class ElaRadioButton;
class ElaText;
class QButtonGroup;
class SettingPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit SettingPage(QWidget *parent = nullptr);
    ~SettingPage() override;

private:
    void initUI();

    QWidget *_centralWidget{nullptr};

    ElaText *_themeLabel{nullptr};
    ElaComboBox *_themeComboBox{nullptr};
    ElaText *_navModeLabel{nullptr};
    ElaRadioButton *_autoButton{nullptr};
    ElaRadioButton *_minimumButton{nullptr};
    ElaRadioButton *_compactButton{nullptr};
    ElaRadioButton *_maximumButton{nullptr};
    QButtonGroup *_navModeGroup{nullptr};
};

#endif

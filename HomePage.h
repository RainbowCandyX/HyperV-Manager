#ifndef HOMEPAGE_H
#define HOMEPAGE_H

#include "BasePage.h"

class QLabel;
class ElaText;
class ElaComboBox;
class ElaToggleSwitch;
class QVBoxLayout;
class HomePage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit HomePage(QWidget* parent = nullptr);
    ~HomePage() override;

    void updateVMStats();

private:
    void loadHostInfo();

    ElaText* _runningCountText{nullptr};
    ElaText* _offCountText{nullptr};
    ElaText* _totalCountText{nullptr};
    ElaText* _pausedCountText{nullptr};

    ElaText* _hypervisorText{nullptr};
    ElaText* _iommuText{nullptr};
    ElaComboBox* _schedulerCombo{nullptr};
    ElaToggleSwitch* _numaSwitch{nullptr};
    ElaText* _vmPathText{nullptr};

    ElaToggleSwitch* _gpuStrategySwitch{nullptr};
    ElaToggleSwitch* _serverSwitch{nullptr};
    ElaText* _serverDescText{nullptr};
};

#endif

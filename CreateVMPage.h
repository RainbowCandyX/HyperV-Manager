#ifndef CREATEVMPAGE_H
#define CREATEVMPAGE_H

#include "BasePage.h"

class ElaLineEdit;
class ElaComboBox;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;
class CreateVMPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit CreateVMPage(QWidget* parent = nullptr);
    ~CreateVMPage() override;

private:
    void onCreateClicked();
    void updateGpuControls();
    void updateSecurityControls();

    ElaLineEdit* _nameEdit{nullptr};
    ElaComboBox* _generationCombo{nullptr};
    ElaText* _generationDescText{nullptr};
    ElaComboBox* _versionCombo{nullptr};
    ElaSpinBox* _memorySpinBox{nullptr};
    ElaToggleSwitch* _dynamicMemorySwitch{nullptr};
    ElaLineEdit* _vhdPathEdit{nullptr};
    ElaSpinBox* _vhdSizeSpinBox{nullptr};
    ElaComboBox* _switchCombo{nullptr};
    ElaLineEdit* _isoPathEdit{nullptr};

    ElaToggleSwitch* _secureBootSwitch{nullptr};
    ElaToggleSwitch* _tpmSwitch{nullptr};
    ElaToggleSwitch* _startAfterSwitch{nullptr};

    ElaComboBox* _isolationCombo{nullptr};
    bool _isolationSupported{false};

    ElaToggleSwitch* _gpuToggle{nullptr};
    ElaComboBox* _gpuCombo{nullptr};
    ElaSpinBox* _gpuPercentSpinBox{nullptr};
    bool _gpuListReady{false};
};

#endif

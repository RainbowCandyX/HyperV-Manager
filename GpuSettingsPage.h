#ifndef GPUSETTINGSPAGE_H
#define GPUSETTINGSPAGE_H

#include "BasePage.h"

class ElaComboBox;
class ElaSpinBox;
class ElaPushButton;
class ElaText;

class GpuSettingsPage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit GpuSettingsPage(QWidget *parent = nullptr);
    ~GpuSettingsPage() override;

private:
    void refreshVmList();
    void refreshGpuList();
    void refreshSelectedVmGpuStatus();
    void applyGpuSettings();
    void removeGpuSettings();
    void updateActionStates();
    QString selectedVmName() const;

    ElaComboBox *_vmCombo{nullptr};
    ElaComboBox *_gpuCombo{nullptr};
    ElaSpinBox *_gpuPercentSpinBox{nullptr};
    ElaPushButton *_applyBtn{nullptr};
    ElaPushButton *_removeBtn{nullptr};
    ElaText *_statusText{nullptr};

    bool _gpuListReady{false};
};

#endif

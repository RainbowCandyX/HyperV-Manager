#ifndef VMSETTINGSDIALOG_H
#define VMSETTINGSDIALOG_H

#include "ElaWidget.h"
#include "HyperVManager.h"

class ElaPivot;
class ElaComboBox;
class ElaLineEdit;
class ElaSpinBox;
class ElaText;
class ElaToggleSwitch;
class ElaTableView;
class ElaPushButton;
class QStandardItemModel;
class QStackedWidget;

class VMSettingsDialog : public ElaWidget
{
    Q_OBJECT
public:
    explicit VMSettingsDialog(const QString& vmName, QWidget *parent = nullptr);

private:
    QWidget* createFormRow(const QString& label, QWidget *field);
    QWidget* createPageContent(const QList<QWidget*>& rows, bool showButtons);
    void loadSettings();
    void applySettings();
    void loadStorage();
    void loadNetworkAdapters();
    void loadProcessorAdvanced();
    void loadMemoryAdvanced();
    void loadGpuStatus();

    QString _vmName;
    bool _isOff{true};

    ElaLineEdit *_nameEdit;
    ElaLineEdit *_notesEdit;

    ElaSpinBox *_cpuSpinBox;

    ElaSpinBox *_memorySpinBox;
    ElaToggleSwitch *_dynamicMemorySwitch;
    ElaSpinBox *_memoryMinSpinBox;
    ElaSpinBox *_memoryMaxSpinBox;

    ElaComboBox *_autoStartCombo;
    ElaComboBox *_autoStopCombo;
    ElaComboBox *_checkpointCombo;

    ElaTableView *_storageTable{nullptr};
    QStandardItemModel *_storageModel{nullptr};

    ElaTableView *_networkTable{nullptr};
    QStandardItemModel *_networkModel{nullptr};
    ElaComboBox *_netSwitchCombo{nullptr};
    QWidget *_netDetailWidget{nullptr};
    ElaText *_netSelectedLabel{nullptr};
    ElaSpinBox *_bwLimitSpinBox{nullptr};
    ElaSpinBox *_bwReserveSpinBox{nullptr};
    ElaToggleSwitch *_vmqSwitch{nullptr};
    ElaToggleSwitch *_ipsecSwitch{nullptr};
    ElaToggleSwitch *_sriovSwitch{nullptr};
    ElaToggleSwitch *_macSpoofSwitch{nullptr};
    ElaToggleSwitch *_dhcpGuardSwitch{nullptr};
    ElaToggleSwitch *_routerGuardSwitch{nullptr};
    QList<VMNetworkAdapterInfo> _cachedAdapters;

    ElaSpinBox *_cpuReserveSpinBox{nullptr};
    ElaSpinBox *_cpuLimitSpinBox{nullptr};
    ElaSpinBox *_cpuWeightSpinBox{nullptr};
    ElaToggleSwitch *_nestedVirtSwitch{nullptr};
    ElaToggleSwitch *_hideHypervisorSwitch{nullptr};

    ElaSpinBox *_memBufferSpinBox{nullptr};
    ElaSpinBox *_memWeightSpinBox{nullptr};

    ElaText *_gpuStatusText{nullptr};
    ElaComboBox *_gpuCombo{nullptr};
    ElaSpinBox *_gpuPercentSpinBox{nullptr};
    ElaText *_gpuDriverLogText{nullptr};

    ElaPivot *_pivot{nullptr};
    QStackedWidget *_stackedWidget{nullptr};
};

#endif

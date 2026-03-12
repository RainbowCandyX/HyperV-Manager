#ifndef VIRTUALMACHINEPAGE_H
#define VIRTUALMACHINEPAGE_H

#include "BasePage.h"

class ElaListView;
class ElaText;
class ElaMenu;
class ElaToolButton;
class QLabel;
class QTimer;
class QStandardItemModel;
class QVBoxLayout;
class VirtualMachinePage : public BasePage
{
    Q_OBJECT
public:
    Q_INVOKABLE explicit VirtualMachinePage(QWidget* parent = nullptr);
    ~VirtualMachinePage() override;

private:
    void refreshVmList();
    void onVmSelectionChanged();
    void refreshPreview();
    void showContextMenu(const QPoint& pos);
    QString selectedVMName() const;

    ElaListView* _vmListView{nullptr};
    QStandardItemModel* _vmListModel{nullptr};
    ElaMenu* _contextMenu{nullptr};

    QWidget* _detailPanel{nullptr};
    QWidget* _emptyHint{nullptr};

    QLabel* _thumbnailLabel{nullptr};
    ElaText* _vmNameLabel{nullptr};
    ElaText* _stateLabel{nullptr};
    ElaText* _uptimeLabel{nullptr};
    ElaText* _cpuInfoLabel{nullptr};
    ElaText* _memoryInfoLabel{nullptr};

    ElaToolButton* _startBtn{nullptr};
    ElaToolButton* _stopBtn{nullptr};
    ElaToolButton* _turnOffBtn{nullptr};
    ElaToolButton* _connectBtn{nullptr};
    ElaToolButton* _settingsBtn{nullptr};
    ElaToolButton* _pauseBtn{nullptr};
    ElaToolButton* _restartBtn{nullptr};

    ElaText* _metricCpuLabel{nullptr};
    ElaText* _metricMemLabel{nullptr};
    ElaText* _metricStorageLabel{nullptr};
    ElaText* _metricNetLabel{nullptr};
    ElaText* _metricGpuLabel{nullptr};

    QWidget* _cpuContent{nullptr};
    QWidget* _memContent{nullptr};
    QWidget* _storageContent{nullptr};
    QWidget* _netContent{nullptr};
    QWidget* _gpuContent{nullptr};
    QVBoxLayout* _cpuContentLayout{nullptr};
    QVBoxLayout* _memContentLayout{nullptr};
    QVBoxLayout* _storageContentLayout{nullptr};
    QVBoxLayout* _netContentLayout{nullptr};
    QVBoxLayout* _gpuContentLayout{nullptr};

    QTimer* _refreshTimer{nullptr};
    QString _selectedVmName;
    bool _thumbnailPending{false};
    bool _summaryPending{false};

    qint64 _baseUptimeSec{0};
    qint64 _baseUptimeTimestamp{0};

    void refreshDeviceInfo();
};

#endif

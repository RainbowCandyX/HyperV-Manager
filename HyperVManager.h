#ifndef HYPERVMANAGER_H
#define HYPERVMANAGER_H

#include <QObject>
#include <QList>
#include <QImage>
#include <QProcess>
#include <functional>

struct VirtualMachineInfo
{
    QString name;
    QString state;
    int cpuCount{0};
    qint64 memoryMB{0};
    qint64 memoryStartupMB{0};
    qint64 uptimeSeconds{0};
    QString notes;
};

struct VMDetailedSettings
{
    QString name;
    int stateValue{0};
    int processorCount{1};
    qint64 memoryStartupMB{1024};
    bool dynamicMemoryEnabled{false};
    qint64 memoryMinimumMB{512};
    qint64 memoryMaximumMB{1048576};
    QString notes;
    QString automaticStartAction;
    QString automaticStopAction;
    QString checkpointType;
};

struct GpuPartitionableInfo
{
    QString name;
    QString instancePath;
    int partitionCount{0};
    QString validPartitionCounts;
};

struct VMGpuPartitionStatus
{
    bool enabled{false};
    QString instancePath;
    int allocationPercent{0};
};

struct SwitchInfo
{
    QString name;
    QString switchType;
    bool allowManagementOS{false};
    QString netAdapterDescription;
};

struct PhysicalAdapterInfo
{
    QString name;
    QString interfaceDescription;
};

struct VMNetworkAdapterInfo
{
    QString adapterName;
    QString switchName;
    QString macAddress;
    bool dynamicMacAddress{true};
    bool macSpoofing{false};
    int vlanId{0};
    bool dhcpGuard{false};
    bool routerGuard{false};
    QString ipAddresses;
    qint64 bandwidthLimitMbps{0};
    qint64 bandwidthReservationMbps{0};
    bool vmqEnabled{false};
    bool ipsecOffloadEnabled{false};
    bool sriovEnabled{false};
};

struct VMStorageInfo
{
    QString controllerType;
    int controllerNumber{0};
    int controllerLocation{0};
    QString path;
    QString diskType;
    qint64 currentSizeMB{0};
    qint64 maxSizeMB{0};
};

struct DDADeviceInfo
{
    QString instanceId;
    QString friendlyName;
    QString deviceClass;
    QString locationPath;
    QString status;
    QString assignedVm;
};

struct HyperVHostInfo
{
    bool hypervisorPresent{false};
    bool iommuAvailable{false};
    QString schedulerType;
    bool numaSpanningEnabled{false};
    QString vmHost;
    QString supportedVersions;
};

struct VMProcessorAdvancedSettings
{
    int count{1};
    int reserve{0};
    int limit{100};
    int weight{100};
    bool nestedVirtualization{false};
    bool exposeVirtualizationExtensions{false};
    QString compatibilityForMigration;
    bool hideHypervisor{false};
};

struct VMSummaryInfo
{
    QString name;
    QString state;
    qint64 memoryUsageMB{0};
    int memoryAvailablePercent{0};
    qint64 uptimeMs{0};
    int cpuUsage{0};
};

struct VMMemoryAdvancedSettings
{
    qint64 startupMB{1024};
    bool dynamicMemoryEnabled{false};
    qint64 minimumMB{512};
    qint64 maximumMB{1048576};
    int buffer{20};
    int weight{5000};
};


class HyperVManager : public QObject
{
    Q_OBJECT
public:
    static HyperVManager* getInstance();

    void refreshVMList();
    QList<VirtualMachineInfo> getVMList() const;

    void startVM(const QString& vmName);
    void stopVM(const QString& vmName);
    void turnOffVM(const QString& vmName);
    void pauseVM(const QString& vmName);
    void resumeVM(const QString& vmName);
    void restartVM(const QString& vmName);
    void saveVM(const QString& vmName);
    void deleteVM(const QString& vmName);
    void connectVM(const QString& vmName);

    void createVM(const QString& name, int generation, qint64 memoryMB,
                  const QString& vhdPath, qint64 vhdSizeGB, const QString& switchName,
                  bool enableGpuPartition = false, int gpuAllocationPercent = 50,
                  const QString& gpuInstancePath = QString());

    void createVMAdvanced(const QString& name, int generation, const QString& version,
                          qint64 memoryMB, bool dynamicMemory,
                          const QString& vhdPath, qint64 vhdSizeGB,
                          const QString& switchName, const QString& isoPath,
                          bool secureBoot, bool enableTpm,
                          bool startAfterCreation,
                          const QString& isolationType = "Disabled",
                          bool enableGpuPartition = false, int gpuAllocationPercent = 50,
                          const QString& gpuInstancePath = QString());

    void getVMSettingsAsync(const QString& vmName,
                            const std::function<void(const VMDetailedSettings&, const QString&)>& callback);
    void applyVMSettings(const QString& originalName, const VMDetailedSettings& settings);

    void getVirtualSwitchesAsync(const std::function<void(const QStringList&, const QString&)>& callback);
    void getSwitchesDetailedAsync(const std::function<void(const QList<SwitchInfo>&, const QString&)>& callback);
    void getPhysicalAdaptersAsync(const std::function<void(const QList<PhysicalAdapterInfo>&, const QString&)>& callback);
    void createSwitch(const QString& name, const QString& type, const QString& adapterDescription = QString());
    void deleteSwitch(const QString& name);

    void getVMNetworkAdaptersAsync(const QString& vmName,
                                   const std::function<void(const QList<VMNetworkAdapterInfo>&, const QString&)>& callback);
    void addVMNetworkAdapter(const QString& vmName, const QString& switchName);
    void removeVMNetworkAdapter(const QString& vmName, const QString& adapterName);
    void setVMNetworkAdapterVlan(const QString& vmName, const QString& adapterName, int vlanId);
    void setVMNetworkAdapterSecurity(const QString& vmName, const QString& adapterName,
                                     bool macSpoofing, bool dhcpGuard, bool routerGuard);
    void applyVMNetworkAdapterQos(const QString& vmName, const QString& adapterName,
                                  qint64 bandwidthLimitMbps, qint64 bandwidthReservationMbps);
    void applyVMNetworkAdapterOffload(const QString& vmName, const QString& adapterName,
                                      bool vmq, bool ipsecOffload, bool sriov);

    void getVMStorageAsync(const QString& vmName,
                           const std::function<void(const QList<VMStorageInfo>&, const QString&)>& callback);
    void addVMHardDisk(const QString& vmName, const QString& path, qint64 sizeGB,
                       const QString& controllerType = "SCSI");
    void addVMExistingDisk(const QString& vmName, const QString& path,
                           const QString& controllerType = "SCSI");
    void removeVMDrive(const QString& vmName, const QString& controllerType,
                       int controllerNumber, int controllerLocation);
    void addVMDvdDrive(const QString& vmName, const QString& isoPath = QString());
    void setVMDvdDrivePath(const QString& vmName, int controllerNumber,
                           int controllerLocation, const QString& isoPath);

    void getPartitionableGpusAsync(const std::function<void(const QList<GpuPartitionableInfo>&, const QString&)>& callback);
    void getVMGpuPartitionStatusAsync(const QString& vmName,
                                      const std::function<void(const VMGpuPartitionStatus&, const QString&)>& callback);
    void applyVMGpuPartitionAsync(const QString& vmName, int gpuAllocationPercent,
                                  const QString& gpuInstancePath = QString());
    void removeVMGpuPartitionAsync(const QString& vmName);
    void installGpuDriversAsync(const QString& vmName,
                                const std::function<void(bool, const QString&)>& callback);

    void getDDADevicesAsync(const std::function<void(const QList<DDADeviceInfo>&, const QString&)>& callback);
    void dismountHostDevice(const QString& instanceId, const QString& locationPath);
    void assignDeviceToVM(const QString& vmName, const QString& locationPath);
    void removeDeviceFromVM(const QString& vmName, const QString& locationPath);
    void mountHostDevice(const QString& instanceId, const QString& locationPath);

    void getHostInfoAsync(const std::function<void(const HyperVHostInfo&, const QString&)>& callback);
    void getSupportedVersionsAsync(const std::function<void(const QStringList&, const QString&)>& callback);
    void getIsolationSupportAsync(const std::function<void(bool supported, const QStringList& types, const QString& error)>& callback);
    void setNumaSpanningAsync(bool enabled, const std::function<void(bool, const QString&)>& callback);
    void setSchedulerTypeAsync(const QString& type, const std::function<void(bool, const QString&)>& callback);

    void checkGpuStrategyAsync(const std::function<void(bool enabled, const QString& error)>& callback);
    void setGpuStrategyAsync(bool enable, const std::function<void(bool success, const QString& error)>& callback);

    void checkIsServerSystemAsync(const std::function<void(bool isServer, bool switchEnabled, const QString& error)>& callback);
    void switchSystemVersionAsync(bool toServer, const std::function<void(bool success, const QString& error)>& callback);

    void getVMThumbnailAsync(const QString& vmName, int width, int height,
                             const std::function<void(const QImage&, const QString&)>& callback);
    void getVMSummaryInfoAsync(const QString& vmName,
                               const std::function<void(const VMSummaryInfo&, const QString&)>& callback);

    void getVMProcessorAdvancedAsync(const QString& vmName,
                                     const std::function<void(const VMProcessorAdvancedSettings&, const QString&)>& callback);
    void applyVMProcessorAdvanced(const QString& vmName, const VMProcessorAdvancedSettings& settings);

    void getVMMemoryAdvancedAsync(const QString& vmName,
                                  const std::function<void(const VMMemoryAdvancedSettings&, const QString&)>& callback);
    void applyVMMemoryAdvanced(const QString& vmName, const VMMemoryAdvancedSettings& settings);

Q_SIGNALS:
    void vmListRefreshed();
    void operationFinished(const QString& vmName, const QString& operation, bool success, const QString& message);
    void errorOccurred(const QString& message);

private:
    explicit HyperVManager(QObject* parent = nullptr);
    void runPowerShellAsync(const QString& command, const QString& vmName, const QString& operation);
    void runPowerShellCallback(const QString& command,
                               const std::function<void(int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)>& callback,
                               int timeoutMs = 30000);

    QList<VirtualMachineInfo> _vmList;
};

#endif

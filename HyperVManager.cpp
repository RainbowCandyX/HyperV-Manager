#include "HyperVManager.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QThread>
#include <memory>
#include <windows.h>

HyperVManager* HyperVManager::getInstance()
{
    static HyperVManager instance;
    return &instance;
}

HyperVManager::HyperVManager(QObject *parent)
    : QObject(parent)
{
}

static void startPowerShell(QProcess *process, const QString& command)
{
    QString wrappedCmd = QString("[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; %1").arg(command);
    process->setProgram("powershell");
    process->setArguments({"-NoProfile", "-NonInteractive", "-Command", wrappedCmd});
    process->start();
}

static QString escapePS(const QString& s)
{
    QString escaped = s;
    escaped.replace("'", "''");
    return escaped;
}

static QString quotePS(const QString& s)
{
    return QString("'%1'").arg(escapePS(s));
}

static QString formatPowerShellError(const QString& stdErr, const QString& stdOut, bool timedOut)
{
    if (timedOut)
    {
        return "PowerShell 命令执行超时（30 秒）";
    }

    QString err = stdErr.trimmed();
    if (!err.isEmpty())
    {
        return err;
    }

    QString out = stdOut.trimmed();
    if (!out.isEmpty())
    {
        return out;
    }

    return "PowerShell 命令执行失败（无详细错误输出）";
}

static int normalizeAllocationPercent(int value)
{
    if (value < 1) return 1;
    if (value > 100) return 100;
    return value;
}

static QJsonArray parseJsonArray(const QString& json, QString *error = nullptr)
{
    if (json.trimmed().isEmpty())
    {
        return {};
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        if (error) *error = parseError.errorString();
        return {};
    }
    if (doc.isArray()) return doc.array();
    if (doc.isObject()) return QJsonArray{doc.object()};
    return {};
}

void HyperVManager::runPowerShellCallback(const QString& command,
                                          const std::function<void(int, const QString&, const QString&, bool)>& callback,
                                          int timeoutMs)
{
    QProcess *process = new QProcess(this);
    QTimer *timer = new QTimer(process);
    timer->setSingleShot(true);

    auto timedOut = std::make_shared<bool>(false);
    connect(timer, &QTimer::timeout, this, [process, timedOut]()
    {
        if (process->state() != QProcess::NotRunning)
        {
            *timedOut = true;
            process->kill();
        }
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [=](int exitCode, QProcess::ExitStatus)
            {
                timer->stop();
                const QString stdErr = QString::fromUtf8(process->readAllStandardError());
                const QString stdOut = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
                process->deleteLater();
                callback(exitCode, stdOut, stdErr, *timedOut);
            });

    timer->start(timeoutMs);
    startPowerShell(process, command);
}

void HyperVManager::runPowerShellAsync(const QString& command, const QString& vmName, const QString& operation)
{
    runPowerShellCallback(command, [this, vmName, operation](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        bool success = (!timedOut && exitCode == 0);
        QString msg;
        if (!success)
        {
            msg = formatPowerShellError(stdErr, stdOut, timedOut);
        }
        Q_EMIT operationFinished(vmName, operation, success, msg);
        if (success)
        {
            refreshVMList();
        }
    });
}

void HyperVManager::refreshVMList()
{
    runPowerShellCallback(
        "Get-VM | Select-Object Name, State, ProcessorCount, "
        "@{N='MemoryMB';E={$_.MemoryAssigned/1MB}}, "
        "@{N='MemoryStartupMB';E={$_.MemoryStartup/1MB}}, "
        "@{N='UptimeSeconds';E={$_.Uptime.TotalSeconds}}, "
        "Notes | ConvertTo-Json -Compress",
        [this](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            _vmList.clear();

            if (timedOut || exitCode != 0)
            {
                const QString detail = formatPowerShellError(stdErr, stdOut, timedOut);
                Q_EMIT errorOccurred(QString("无法获取虚拟机列表：%1").arg(detail));
                Q_EMIT vmListRefreshed();
                return;
            }

            if (stdOut.isEmpty())
            {
                Q_EMIT vmListRefreshed();
                return;
            }

            QJsonArray arr = parseJsonArray(stdOut);
            for (const QJsonValue& val : arr)
            {
                QJsonObject obj = val.toObject();
                VirtualMachineInfo vm;
                vm.name = obj["Name"].toString();
                int stateVal = obj["State"].toInt();
                switch (stateVal)
                {
                    case 2: vm.state = "Running"; break;
                    case 3: vm.state = "Off"; break;
                    case 6: vm.state = "Saved"; break;
                    case 9: vm.state = "Paused"; break;
                    default: vm.state = QString("Unknown(%1)").arg(stateVal); break;
                }
                vm.cpuCount = obj["ProcessorCount"].toInt();
                vm.memoryMB = static_cast<qint64>(obj["MemoryMB"].toDouble());
                vm.memoryStartupMB = static_cast<qint64>(obj["MemoryStartupMB"].toDouble());
                vm.uptimeSeconds = static_cast<qint64>(obj["UptimeSeconds"].toDouble());
                vm.notes = obj["Notes"].toString();
                _vmList.append(vm);
            }
            Q_EMIT vmListRefreshed();
        });
}

QList<VirtualMachineInfo> HyperVManager::getVMList() const
{
    return _vmList;
}

void HyperVManager::startVM(const QString& vmName)
{
    runPowerShellAsync(QString("Start-VM -Name %1").arg(quotePS(vmName)), vmName, "启动");
}

void HyperVManager::stopVM(const QString& vmName)
{
    runPowerShellAsync(QString("Stop-VM -Name %1").arg(quotePS(vmName)), vmName, "关闭");
}

void HyperVManager::turnOffVM(const QString& vmName)
{
    runPowerShellAsync(QString("Stop-VM -Name %1 -TurnOff").arg(quotePS(vmName)), vmName, "强制关机");
}

void HyperVManager::pauseVM(const QString& vmName)
{
    runPowerShellAsync(QString("Suspend-VM -Name %1").arg(quotePS(vmName)), vmName, "暂停");
}

void HyperVManager::resumeVM(const QString& vmName)
{
    runPowerShellAsync(QString("Resume-VM -Name %1").arg(quotePS(vmName)), vmName, "恢复");
}

void HyperVManager::restartVM(const QString& vmName)
{
    runPowerShellAsync(QString("Restart-VM -Name %1 -Force").arg(quotePS(vmName)), vmName, "重启");
}

void HyperVManager::saveVM(const QString& vmName)
{
    runPowerShellAsync(QString("Save-VM -Name %1").arg(quotePS(vmName)), vmName, "保存");
}

void HyperVManager::deleteVM(const QString& vmName)
{
    runPowerShellAsync(QString("Remove-VM -Name %1 -Force").arg(quotePS(vmName)), vmName, "删除");
}

void HyperVManager::connectVM(const QString& vmName)
{
    QProcess::startDetached("vmconnect", {"localhost", vmName});
}

static QStringList buildGpuPartitionConfigCommands(const QString& vmName, int gpuAllocationPercent,
                                                   const QString& gpuInstancePath, bool replaceExistingAdapter)
{
    QStringList cmds;
    const int allocationPercent = normalizeAllocationPercent(gpuAllocationPercent);
    const QString safeVmName = quotePS(vmName);

    if (gpuInstancePath.isEmpty())
    {
        cmds << "$selectedGpu = Get-VMHostPartitionableGpu | Select-Object -First 1";
    }
    else
    {
        cmds << QString("$selectedGpu = Get-VMHostPartitionableGpu | Where-Object { $_.InstancePath -eq %1 } | Select-Object -First 1")
                    .arg(quotePS(gpuInstancePath));
    }
    cmds << "if (-not $selectedGpu) { throw '未检测到可用的 GPU-P 设备' }";
    cmds << QString("if ((Get-VM -Name %1).State -ne 'Off') { throw '虚拟机必须处于关闭状态才能配置 GPU-P' }")
                .arg(safeVmName);

    cmds << QString("Set-VM -Name %1 -GuestControlledCacheTypes $true -LowMemoryMappedIoSpace 1GB -HighMemoryMappedIoSpace 32GB")
                .arg(safeVmName);

    if (replaceExistingAdapter)
    {
        cmds << QString("$existingAdapter = Get-VMGpuPartitionAdapter -VMName %1 -ErrorAction SilentlyContinue")
                    .arg(safeVmName);
        cmds << QString("if ($existingAdapter) { Remove-VMGpuPartitionAdapter -VMName %1 }")
                    .arg(safeVmName);
    }

    if (gpuInstancePath.isEmpty())
    {
        cmds << QString("Add-VMGpuPartitionAdapter -VMName %1").arg(safeVmName);
    }
    else
    {
        cmds << QString("Add-VMGpuPartitionAdapter -VMName %1 -InstancePath %2")
                    .arg(safeVmName, quotePS(gpuInstancePath));
    }

    cmds << QString("$scale = %1 / 100.0").arg(allocationPercent);
    cmds << "$minVRAM = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MinPartitionVRAM * $scale)))";
    cmds << "$maxVRAM = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MaxPartitionVRAM * $scale)))";
    cmds << "$optVRAM = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.OptimalPartitionVRAM * $scale)))";
    cmds << "$minEncode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MinPartitionEncode * $scale)))";
    cmds << "$maxEncode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MaxPartitionEncode * $scale)))";
    cmds << "$optEncode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.OptimalPartitionEncode * $scale)))";
    cmds << "$minDecode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MinPartitionDecode * $scale)))";
    cmds << "$maxDecode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MaxPartitionDecode * $scale)))";
    cmds << "$optDecode = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.OptimalPartitionDecode * $scale)))";
    cmds << "$minCompute = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MinPartitionCompute * $scale)))";
    cmds << "$maxCompute = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.MaxPartitionCompute * $scale)))";
    cmds << "$optCompute = [UInt64]([Math]::Max([double]1, [Math]::Floor($selectedGpu.OptimalPartitionCompute * $scale)))";
    cmds << QString("Set-VMGpuPartitionAdapter -VMName %1 "
                    "-MinPartitionVRAM $minVRAM -MaxPartitionVRAM $maxVRAM -OptimalPartitionVRAM $optVRAM "
                    "-MinPartitionEncode $minEncode -MaxPartitionEncode $maxEncode -OptimalPartitionEncode $optEncode "
                    "-MinPartitionDecode $minDecode -MaxPartitionDecode $maxDecode -OptimalPartitionDecode $optDecode "
                    "-MinPartitionCompute $minCompute -MaxPartitionCompute $maxCompute -OptimalPartitionCompute $optCompute")
                .arg(safeVmName);

    return cmds;
}

void HyperVManager::createVM(const QString& name, int generation, qint64 memoryMB,
                             const QString& vhdPath, qint64 vhdSizeGB, const QString& switchName,
                             bool enableGpuPartition, int gpuAllocationPercent, const QString& gpuInstancePath)
{
    QStringList cmds;
    cmds << QString("New-VM -Name %1 -Generation %2 -MemoryStartupBytes %3MB "
                    "-NewVHDPath %4 -NewVHDSizeBytes %5GB")
               .arg(quotePS(name))
               .arg(generation)
               .arg(memoryMB)
               .arg(quotePS(vhdPath))
               .arg(vhdSizeGB);

    if (!switchName.isEmpty())
    {
        cmds.last() += QString(" -SwitchName %1").arg(quotePS(switchName));
    }

    if (enableGpuPartition)
    {
        cmds += buildGpuPartitionConfigCommands(name, gpuAllocationPercent, gpuInstancePath, false);
    }

    runPowerShellAsync(cmds.join("; "), name, enableGpuPartition ? "创建并配置 GPU-P" : "创建");
}

void HyperVManager::createVMAdvanced(const QString& name, int generation, const QString& version,
                                     qint64 memoryMB, bool dynamicMemory,
                                     const QString& vhdPath, qint64 vhdSizeGB,
                                     const QString& switchName, const QString& isoPath,
                                     bool secureBoot, bool enableTpm,
                                     bool startAfterCreation,
                                     const QString& isolationType,
                                     bool enableGpuPartition, int gpuAllocationPercent,
                                     const QString& gpuInstancePath)
{
    QStringList cmds;
    const QString safeName = quotePS(name);

    QString newVmCmd = QString("New-VM -Name %1 -Generation %2 -MemoryStartupBytes %3MB -Force")
                           .arg(safeName).arg(generation).arg(memoryMB);

    if (!version.isEmpty())
    {
        newVmCmd += QString(" -Version %1").arg(version);
    }

    if (!vhdPath.isEmpty() && vhdSizeGB > 0)
    {
        newVmCmd += QString(" -NewVHDPath %1 -NewVHDSizeBytes %2GB").arg(quotePS(vhdPath)).arg(vhdSizeGB);
    }
    else if (!vhdPath.isEmpty())
    {
        newVmCmd += QString(" -VHDPath %1").arg(quotePS(vhdPath));
    }

    if (!switchName.isEmpty())
    {
        newVmCmd += QString(" -SwitchName %1").arg(quotePS(switchName));
    }

    if (generation == 2 && !isolationType.isEmpty() && isolationType != "Disabled")
    {
        bool versionOk = false;
        double ver = version.toDouble(&versionOk);
        if (!versionOk || ver >= 10.0)
        {
            newVmCmd += QString(" -GuestStateIsolationType %1").arg(isolationType);
        }
    }
    cmds << newVmCmd;

    if (dynamicMemory)
    {
        cmds << QString("Set-VMMemory -VMName %1 -DynamicMemoryEnabled $true").arg(safeName);
    }

    if (generation == 2)
    {
        if (secureBoot)
        {
            cmds << QString("Set-VMFirmware -VMName %1 -EnableSecureBoot On").arg(safeName);
        }
        if (enableTpm)
        {
            cmds << QString("Set-VMSecurity -VMName %1 -EncryptStateAndVmMigrationTraffic $true").arg(safeName);
            cmds << QString("Set-VMKeyProtector -VMName %1 -NewLocalKeyProtector").arg(safeName);
            cmds << QString("Enable-VMTPM -VMName %1").arg(safeName);
        }
    }

    if (!isoPath.isEmpty())
    {
        cmds << QString("$dvd = Get-VMDvdDrive -VMName %1; "
                        "if (-not $dvd) { Add-VMDvdDrive -VMName %1 }; "
                        "Set-VMDvdDrive -VMName %1 -Path %2")
                    .arg(safeName, quotePS(isoPath));
        if (generation == 2)
        {
            cmds << QString("$d = Get-VMDvdDrive -VMName %1; Set-VMFirmware -VMName %1 -FirstBootDevice $d").arg(safeName);
        }
    }

    if (enableGpuPartition)
    {
        cmds += buildGpuPartitionConfigCommands(name, gpuAllocationPercent, gpuInstancePath, false);
    }

    if (startAfterCreation)
    {
        cmds << QString("Start-VM -Name %1").arg(safeName);
    }

    QString opDesc = "创建";
    if (enableGpuPartition) opDesc += " + GPU-P";
    if (startAfterCreation) opDesc += " + 启动";

    runPowerShellAsync(cmds.join("; "), name, opDesc);
}

void HyperVManager::getVMSettingsAsync(const QString& vmName,
                                       const std::function<void(const VMDetailedSettings&, const QString&)>& callback)
{
    const QString cmd = QString(
                            "Get-VM -Name %1 | Select-Object Name, State, ProcessorCount, "
                            "@{N='MemoryStartupMB';E={$_.MemoryStartup/1MB}}, "
                            "DynamicMemoryEnabled, "
                            "@{N='MemoryMinimumMB';E={$_.MemoryMinimum/1MB}}, "
                            "@{N='MemoryMaximumMB';E={$_.MemoryMaximum/1MB}}, "
                            "Notes, "
                            "@{N='AutomaticStartAction';E={$_.AutomaticStartAction.ToString()}}, "
                            "@{N='AutomaticStopAction';E={$_.AutomaticStopAction.ToString()}}, "
                            "@{N='CheckpointType';E={$_.CheckpointType.ToString()}} "
                            "| ConvertTo-Json -Compress")
                        .arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        VMDetailedSettings settings;
        if (timedOut || exitCode != 0)
        {
            callback(settings, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            callback(settings, QString("读取虚拟机设置失败：%1").arg(parseError.errorString()));
            return;
        }

        QJsonObject obj = doc.object();
        settings.name = obj["Name"].toString();
        settings.stateValue = obj["State"].toInt();
        settings.processorCount = obj["ProcessorCount"].toInt();
        settings.memoryStartupMB = static_cast<qint64>(obj["MemoryStartupMB"].toDouble());
        settings.dynamicMemoryEnabled = obj["DynamicMemoryEnabled"].toBool();
        settings.memoryMinimumMB = static_cast<qint64>(obj["MemoryMinimumMB"].toDouble());
        settings.memoryMaximumMB = static_cast<qint64>(obj["MemoryMaximumMB"].toDouble());
        settings.notes = obj["Notes"].toString();
        settings.automaticStartAction = obj["AutomaticStartAction"].toString();
        settings.automaticStopAction = obj["AutomaticStopAction"].toString();
        settings.checkpointType = obj["CheckpointType"].toString();
        callback(settings, {});
    });
}

void HyperVManager::applyVMSettings(const QString& originalName, const VMDetailedSettings& settings)
{
    QStringList cmds;
    const QString safeName = quotePS(originalName);
    const QStringList allowedStartActions = {"Nothing", "StartIfRunning", "Start"};
    const QStringList allowedStopActions = {"TurnOff", "Save", "ShutDown"};
    const QStringList allowedCheckpointTypes = {"Disabled", "Standard", "Production", "ProductionOnly"};
    const QString automaticStartAction = allowedStartActions.contains(settings.automaticStartAction)
                                             ? settings.automaticStartAction : "Nothing";
    const QString automaticStopAction = allowedStopActions.contains(settings.automaticStopAction)
                                            ? settings.automaticStopAction : "TurnOff";
    const QString checkpointType = allowedCheckpointTypes.contains(settings.checkpointType)
                                       ? settings.checkpointType : "Standard";

    cmds << QString("Set-VM -Name %1 -Notes %2 -AutomaticStartAction %3 -AutomaticStopAction %4 -CheckpointType %5")
            .arg(safeName, quotePS(settings.notes), automaticStartAction, automaticStopAction, checkpointType);

    if (settings.stateValue == 3)
    {
        cmds << QString("Set-VMProcessor -VMName %1 -Count %2")
                .arg(safeName).arg(settings.processorCount);

        if (settings.dynamicMemoryEnabled)
        {
            cmds << QString("Set-VMMemory -VMName %1 -StartupBytes %2MB -DynamicMemoryEnabled $true -MinimumBytes %3MB -MaximumBytes %4MB")
                    .arg(safeName).arg(settings.memoryStartupMB).arg(settings.memoryMinimumMB).arg(settings.memoryMaximumMB);
        }
        else
        {
            cmds << QString("Set-VMMemory -VMName %1 -StartupBytes %2MB -DynamicMemoryEnabled $false")
                    .arg(safeName).arg(settings.memoryStartupMB);
        }
    }

    if (settings.name != originalName)
    {
        cmds << QString("Rename-VM -Name %1 -NewName %2")
                .arg(safeName, quotePS(settings.name));
    }

    runPowerShellAsync(cmds.join("; "), originalName, "设置");
}

void HyperVManager::getVirtualSwitchesAsync(const std::function<void(const QStringList&, const QString&)>& callback)
{
    runPowerShellCallback("Get-VMSwitch | Select-Object -ExpandProperty Name",
        [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            if (timedOut || exitCode != 0)
            {
                callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
                return;
            }
            QStringList switches;
            for (const QString& line : stdOut.split('\n', Qt::SkipEmptyParts))
            {
                const QString trimmed = line.trimmed();
                if (!trimmed.isEmpty()) switches.append(trimmed);
            }
            callback(switches, {});
        });
}

void HyperVManager::getSwitchesDetailedAsync(const std::function<void(const QList<SwitchInfo>&, const QString&)>& callback)
{
    const QString cmd = "Get-VMSwitch | Select-Object Name, "
                        "@{N='SwitchType';E={$_.SwitchType.ToString()}}, "
                        "AllowManagementOS, NetAdapterInterfaceDescription "
                        "| ConvertTo-Json -Compress";

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }

        QList<SwitchInfo> switches;
        QJsonArray arr = parseJsonArray(stdOut);
        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            SwitchInfo info;
            info.name = obj["Name"].toString();
            info.switchType = obj["SwitchType"].toString();
            info.allowManagementOS = obj["AllowManagementOS"].toBool();
            info.netAdapterDescription = obj["NetAdapterInterfaceDescription"].toString();
            switches.append(info);
        }
        callback(switches, {});
    });
}

void HyperVManager::getPhysicalAdaptersAsync(const std::function<void(const QList<PhysicalAdapterInfo>&, const QString&)>& callback)
{
    const QString cmd = "Get-NetAdapter -Physical | Select-Object Name, InterfaceDescription | ConvertTo-Json -Compress";

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        QList<PhysicalAdapterInfo> adapters;
        QJsonArray arr = parseJsonArray(stdOut);
        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            PhysicalAdapterInfo info;
            info.name = obj["Name"].toString();
            info.interfaceDescription = obj["InterfaceDescription"].toString();
            adapters.append(info);
        }
        callback(adapters, {});
    });
}

void HyperVManager::createSwitch(const QString& name, const QString& type, const QString& adapterDescription)
{
    QString cmd;
    if (type == "External" && !adapterDescription.isEmpty())
    {
        cmd = QString("New-VMSwitch -Name %1 -NetAdapterInterfaceDescription %2 -AllowManagementOS $true")
                  .arg(quotePS(name), quotePS(adapterDescription));
    }
    else if (type == "Internal")
    {
        cmd = QString("New-VMSwitch -Name %1 -SwitchType Internal").arg(quotePS(name));
    }
    else
    {
        cmd = QString("New-VMSwitch -Name %1 -SwitchType Private").arg(quotePS(name));
    }
    runPowerShellAsync(cmd, name, "创建交换机");
}

void HyperVManager::deleteSwitch(const QString& name)
{
    runPowerShellAsync(QString("Remove-VMSwitch -Name %1 -Force").arg(quotePS(name)), name, "删除交换机");
}

void HyperVManager::getVMNetworkAdaptersAsync(const QString& vmName,
                                              const std::function<void(const QList<VMNetworkAdapterInfo>&, const QString&)>& callback)
{
    const QString cmd = QString(
        "Get-VMNetworkAdapter -VMName %1 | Select-Object Name, SwitchName, MacAddress, "
        "DynamicMacAddressEnabled, "
        "@{N='MacSpoofing';E={$_.MacAddressSpoofing -eq 'On'}}, "
        "@{N='VlanId';E={(Get-VMNetworkAdapterVlan -VMNetworkAdapter $_).AccessVlanId}}, "
        "@{N='DhcpGuard';E={$_.DhcpGuard -eq 'On'}}, "
        "@{N='RouterGuard';E={$_.RouterGuard -eq 'On'}}, "
        "@{N='IpAddresses';E={($_.IPAddresses -join ', ')}}, "
        "@{N='MaxBandwidth';E={if($_.BandwidthSetting){[math]::Floor($_.BandwidthSetting.MaximumBandwidth/1000000)}else{0}}}, "
        "@{N='MinBandwidth';E={if($_.BandwidthSetting){[math]::Floor($_.BandwidthSetting.MinimumBandwidthAbsolute/1000000)}else{0}}}, "
        "@{N='VmqWeight';E={$_.VmqWeight}}, "
        "@{N='IovWeight';E={$_.IovWeight}}, "
        "@{N='IPsecOffloadMaxSA';E={$_.IPsecOffloadMaxSA}} "
        "| ConvertTo-Json -Compress").arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        QList<VMNetworkAdapterInfo> adapters;
        QJsonArray arr = parseJsonArray(stdOut);
        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            VMNetworkAdapterInfo info;
            info.adapterName = obj["Name"].toString();
            info.switchName = obj["SwitchName"].toString();
            info.macAddress = obj["MacAddress"].toString();
            info.dynamicMacAddress = obj["DynamicMacAddressEnabled"].toBool(true);
            info.macSpoofing = obj["MacSpoofing"].toBool(false);
            info.vlanId = obj["VlanId"].toInt(0);
            info.dhcpGuard = obj["DhcpGuard"].toBool(false);
            info.routerGuard = obj["RouterGuard"].toBool(false);
            info.ipAddresses = obj["IpAddresses"].toString();
            info.bandwidthLimitMbps = static_cast<qint64>(obj["MaxBandwidth"].toDouble(0));
            info.bandwidthReservationMbps = static_cast<qint64>(obj["MinBandwidth"].toDouble(0));
            info.vmqEnabled = obj["VmqWeight"].toInt(0) > 0;
            info.ipsecOffloadEnabled = obj["IPsecOffloadMaxSA"].toInt(0) > 0;
            info.sriovEnabled = obj["IovWeight"].toInt(0) > 0;
            adapters.append(info);
        }
        callback(adapters, {});
    });
}

void HyperVManager::addVMNetworkAdapter(const QString& vmName, const QString& switchName)
{
    QString cmd = QString("Add-VMNetworkAdapter -VMName %1").arg(quotePS(vmName));
    if (!switchName.isEmpty())
    {
        cmd += QString(" -SwitchName %1").arg(quotePS(switchName));
    }
    runPowerShellAsync(cmd, vmName, "添加网络适配器");
}

void HyperVManager::removeVMNetworkAdapter(const QString& vmName, const QString& adapterName)
{
    const QString cmd = QString("Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | Remove-VMNetworkAdapter")
                            .arg(quotePS(vmName), quotePS(adapterName));
    runPowerShellAsync(cmd, vmName, "移除网络适配器");
}

void HyperVManager::setVMNetworkAdapterVlan(const QString& vmName, const QString& adapterName, int vlanId)
{
    QString cmd;
    if (vlanId <= 0)
    {
        cmd = QString("Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | Set-VMNetworkAdapterVlan -Untagged")
                  .arg(quotePS(vmName), quotePS(adapterName));
    }
    else
    {
        cmd = QString("Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | Set-VMNetworkAdapterVlan -Access -VlanId %3")
                  .arg(quotePS(vmName), quotePS(adapterName)).arg(vlanId);
    }
    runPowerShellAsync(cmd, vmName, "设置 VLAN");
}

void HyperVManager::setVMNetworkAdapterSecurity(const QString& vmName, const QString& adapterName,
                                                 bool macSpoofing, bool dhcpGuard, bool routerGuard)
{
    const QString cmd = QString(
        "Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | "
        "Set-VMNetworkAdapter -MacAddressSpoofing %3 -DhcpGuard %4 -RouterGuard %5")
        .arg(quotePS(vmName), quotePS(adapterName),
             macSpoofing ? "On" : "Off",
             dhcpGuard ? "On" : "Off",
             routerGuard ? "On" : "Off");
    runPowerShellAsync(cmd, vmName, "设置网络安全");
}

void HyperVManager::applyVMNetworkAdapterQos(const QString& vmName, const QString& adapterName,
                                              qint64 bandwidthLimitMbps, qint64 bandwidthReservationMbps)
{
    const qint64 limitBps = bandwidthLimitMbps * 1000000LL;
    const qint64 reserveBps = bandwidthReservationMbps * 1000000LL;
    const QString cmd = QString(
        "Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | "
        "Set-VMNetworkAdapter -MaximumBandwidth %3 -MinimumBandwidthAbsolute %4")
        .arg(quotePS(vmName), quotePS(adapterName))
        .arg(limitBps)
        .arg(reserveBps);
    runPowerShellAsync(cmd, vmName, "设置网络 QoS");
}

void HyperVManager::applyVMNetworkAdapterOffload(const QString& vmName, const QString& adapterName,
                                                  bool vmq, bool ipsecOffload, bool sriov)
{
    const QString cmd = QString(
        "Get-VMNetworkAdapter -VMName %1 | Where-Object { $_.Name -eq %2 } | "
        "Set-VMNetworkAdapter -VmqWeight %3 -IovWeight %4 -IPsecOffloadMaximumSecurityAssociation %5")
        .arg(quotePS(vmName), quotePS(adapterName))
        .arg(vmq ? 100 : 0)
        .arg(sriov ? 100 : 0)
        .arg(ipsecOffload ? 512 : 0);
    runPowerShellAsync(cmd, vmName, "设置硬件加速");
}

void HyperVManager::getVMStorageAsync(const QString& vmName,
                                      const std::function<void(const QList<VMStorageInfo>&, const QString&)>& callback)
{
    const QString cmd = QString(
        "$disks = @(); "
        "Get-VMHardDiskDrive -VMName %1 | ForEach-Object { "
        "  $vhd = $null; if ($_.Path) { $vhd = Get-VHD -Path $_.Path -ErrorAction SilentlyContinue }; "
        "  $disks += [PSCustomObject]@{ "
        "    ControllerType=$_.ControllerType.ToString(); ControllerNumber=$_.ControllerNumber; "
        "    ControllerLocation=$_.ControllerLocation; Path=$_.Path; DiskType='VirtualHardDisk'; "
        "    CurrentSizeMB=if($vhd){[Math]::Round($vhd.FileSize/1MB)}else{0}; "
        "    MaxSizeMB=if($vhd){[Math]::Round($vhd.Size/1MB)}else{0} } }; "
        "Get-VMDvdDrive -VMName %1 | ForEach-Object { "
        "  $disks += [PSCustomObject]@{ "
        "    ControllerType=$_.ControllerType.ToString(); ControllerNumber=$_.ControllerNumber; "
        "    ControllerLocation=$_.ControllerLocation; Path=$_.Path; DiskType='DVD'; "
        "    CurrentSizeMB=0; MaxSizeMB=0 } }; "
        "$disks | ConvertTo-Json -Compress").arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        QList<VMStorageInfo> storage;
        QJsonArray arr = parseJsonArray(stdOut);
        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            VMStorageInfo info;
            info.controllerType = obj["ControllerType"].toString();
            info.controllerNumber = obj["ControllerNumber"].toInt();
            info.controllerLocation = obj["ControllerLocation"].toInt();
            info.path = obj["Path"].toString();
            info.diskType = obj["DiskType"].toString();
            info.currentSizeMB = static_cast<qint64>(obj["CurrentSizeMB"].toDouble());
            info.maxSizeMB = static_cast<qint64>(obj["MaxSizeMB"].toDouble());
            storage.append(info);
        }
        callback(storage, {});
    });
}

void HyperVManager::addVMHardDisk(const QString& vmName, const QString& path, qint64 sizeGB,
                                   const QString& controllerType)
{
    const QString cmd = QString("New-VHD -Path %1 -SizeBytes %2GB -Dynamic; "
                                "Add-VMHardDiskDrive -VMName %3 -ControllerType %4 -Path %1")
                            .arg(quotePS(path)).arg(sizeGB).arg(quotePS(vmName), controllerType);
    runPowerShellAsync(cmd, vmName, "添加硬盘");
}

void HyperVManager::addVMExistingDisk(const QString& vmName, const QString& path,
                                       const QString& controllerType)
{
    const QString cmd = QString("Add-VMHardDiskDrive -VMName %1 -ControllerType %2 -Path %3")
                            .arg(quotePS(vmName), controllerType, quotePS(path));
    runPowerShellAsync(cmd, vmName, "添加已有硬盘");
}

void HyperVManager::removeVMDrive(const QString& vmName, const QString& controllerType,
                                   int controllerNumber, int controllerLocation)
{
    QString cmd;
    if (controllerType == "DVD" || controllerType == "IDE")
    {
        cmd = QString("$dvd = Get-VMDvdDrive -VMName %1 -ControllerNumber %2 -ControllerLocation %3 -ErrorAction SilentlyContinue; "
                      "if ($dvd) { Remove-VMDvdDrive -VMName %1 -ControllerNumber %2 -ControllerLocation %3 } "
                      "else { Remove-VMHardDiskDrive -VMName %1 -ControllerType %4 -ControllerNumber %2 -ControllerLocation %3 }")
                  .arg(quotePS(vmName)).arg(controllerNumber).arg(controllerLocation).arg(controllerType);
    }
    else
    {
        cmd = QString("Remove-VMHardDiskDrive -VMName %1 -ControllerType %2 -ControllerNumber %3 -ControllerLocation %4")
                  .arg(quotePS(vmName), controllerType).arg(controllerNumber).arg(controllerLocation);
    }
    runPowerShellAsync(cmd, vmName, "移除磁盘");
}

void HyperVManager::addVMDvdDrive(const QString& vmName, const QString& isoPath)
{
    QString cmd = QString("Add-VMDvdDrive -VMName %1").arg(quotePS(vmName));
    if (!isoPath.isEmpty())
    {
        cmd += QString(" -Path %1").arg(quotePS(isoPath));
    }
    runPowerShellAsync(cmd, vmName, "添加 DVD 驱动器");
}

void HyperVManager::setVMDvdDrivePath(const QString& vmName, int controllerNumber,
                                       int controllerLocation, const QString& isoPath)
{
    const QString cmd = QString("Set-VMDvdDrive -VMName %1 -ControllerNumber %2 -ControllerLocation %3 -Path %4")
                            .arg(quotePS(vmName)).arg(controllerNumber).arg(controllerLocation)
                            .arg(isoPath.isEmpty() ? "$null" : quotePS(isoPath));
    runPowerShellAsync(cmd, vmName, "设置 DVD 路径");
}

void HyperVManager::getPartitionableGpusAsync(const std::function<void(const QList<GpuPartitionableInfo>&, const QString&)>& callback)
{
    const QString cmd =
        "$vcList = Get-CimInstance -ClassName Win32_VideoController | Select-Object Name, PNPDeviceID; "
        "Get-VMHostPartitionableGpu | ForEach-Object { "
        "  $gpu = $_; $friendlyName = ''; "
        "  $gpuIdParts = $gpu.Name -replace '.*PCI#', '' -replace '#.*', '' -split '&' | "
        "    Where-Object { $_ -match '^(VEN|DEV)_' }; "
        "  if ($gpuIdParts.Count -ge 2) { "
        "    $matchPattern = ($gpuIdParts[0] + '&' + $gpuIdParts[1]).ToUpper(); "
        "    foreach ($vc in $vcList) { "
        "      if ($vc.PNPDeviceID -and $vc.PNPDeviceID.ToUpper().Contains($matchPattern)) { "
        "        $friendlyName = $vc.Name; break "
        "      } "
        "    } "
        "  }; "
        "  [PSCustomObject]@{ "
        "    Name = if($friendlyName){$friendlyName}else{$gpu.Name}; "
        "    InstancePath = $gpu.InstancePath; "
        "    PartitionCount = $gpu.PartitionCount; "
        "    ValidPartitionCounts = $gpu.ValidPartitionCounts "
        "  } "
        "} | ConvertTo-Json -Compress";

    runPowerShellCallback(cmd,
        [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            if (timedOut || exitCode != 0)
            {
                callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
                return;
            }
            if (stdOut.isEmpty())
            {
                callback({}, {});
                return;
            }
            QString error;
            QJsonArray arr = parseJsonArray(stdOut, &error);
            if (!error.isEmpty())
            {
                callback({}, QString("解析 GPU 列表失败：%1").arg(error));
                return;
            }
            QList<GpuPartitionableInfo> gpus;
            for (const QJsonValue& value : arr)
            {
                const QJsonObject obj = value.toObject();
                GpuPartitionableInfo info;
                info.name = obj["Name"].toString();
                info.instancePath = obj["InstancePath"].toString();
                info.partitionCount = obj["PartitionCount"].toInt();
                const QJsonValue validCounts = obj["ValidPartitionCounts"];
                if (validCounts.isArray())
                {
                    QStringList parts;
                    for (const QJsonValue& v : validCounts.toArray())
                        parts << QString::number(v.toInt());
                    info.validPartitionCounts = parts.join(",");
                }
                else
                {
                    info.validPartitionCounts = validCounts.toString();
                }
                if (!info.name.isEmpty() || !info.instancePath.isEmpty())
                    gpus.append(info);
            }
            callback(gpus, {});
        });
}

void HyperVManager::getVMGpuPartitionStatusAsync(const QString& vmName,
                                                 const std::function<void(const VMGpuPartitionStatus&, const QString&)>& callback)
{
    const QString cmd = QString(
                            "$adapter = Get-VMGpuPartitionAdapter -VMName %1 -ErrorAction SilentlyContinue; "
                            "if (-not $adapter) { "
                            "  [PSCustomObject]@{ Enabled = $false; InstancePath = ''; AllocationPercent = 0 } | ConvertTo-Json -Compress; "
                            "} else { "
                            "  $gpu = $null; "
                            "  if ($adapter.InstancePath) { "
                            "    $gpu = Get-VMHostPartitionableGpu | Where-Object { $_.InstancePath -eq $adapter.InstancePath } | Select-Object -First 1; "
                            "  } "
                            "  $percent = 50; "
                            "  if ($gpu -and $gpu.OptimalPartitionVRAM -gt 0 -and $adapter.OptimalPartitionVRAM -gt 0) { "
                            "    $percent = [int][Math]::Round(($adapter.OptimalPartitionVRAM * 100.0) / $gpu.OptimalPartitionVRAM); "
                            "  } "
                            "  [PSCustomObject]@{ Enabled = $true; InstancePath = $adapter.InstancePath; AllocationPercent = $percent } | ConvertTo-Json -Compress; "
                            "}")
                        .arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        VMGpuPartitionStatus status;
        if (timedOut || exitCode != 0)
        {
            callback(status, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        if (stdOut.isEmpty())
        {
            callback(status, {});
            return;
        }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            callback(status, QString("解析 GPU-P 状态失败：%1").arg(parseError.errorString()));
            return;
        }
        const QJsonObject obj = doc.object();
        status.enabled = obj["Enabled"].toBool(false);
        status.instancePath = obj["InstancePath"].toString();
        status.allocationPercent = normalizeAllocationPercent(obj["AllocationPercent"].toInt(50));
        callback(status, {});
    });
}

void HyperVManager::applyVMGpuPartitionAsync(const QString& vmName, int gpuAllocationPercent, const QString& gpuInstancePath)
{
    const QStringList cmds = buildGpuPartitionConfigCommands(vmName, gpuAllocationPercent, gpuInstancePath, true);
    runPowerShellAsync(cmds.join("; "), vmName, "应用 GPU-P 设置");
}

void HyperVManager::removeVMGpuPartitionAsync(const QString& vmName)
{
    const QString safeVmName = quotePS(vmName);
    const QString cmd = QString(
                            "$adapter = Get-VMGpuPartitionAdapter -VMName %1 -ErrorAction SilentlyContinue; "
                            "if ($adapter) { Remove-VMGpuPartitionAdapter -VMName %1 }")
                        .arg(safeVmName);
    runPowerShellAsync(cmd, vmName, "移除 GPU-P");
}

void HyperVManager::installGpuDriversAsync(const QString& vmName,
                                            const std::function<void(bool, const QString&)>& callback)
{
    const QString safeVmName = quotePS(vmName);

    const QString cmd = QString(
        "$ErrorActionPreference = 'Stop'; "
        "$vmName = %1; "
        "$log = @(); "

        "$vhd = (Get-VMHardDiskDrive -VMName $vmName | Select-Object -First 1).Path; "
        "if (-not $vhd) { throw '未找到虚拟硬盘' }; "
        "$log += \"找到磁盘: $vhd\"; "

        "Dismount-DiskImage -ImagePath $vhd -ErrorAction SilentlyContinue; "
        "$img = Mount-DiskImage -ImagePath $vhd -NoDriveLetter -PassThru -ErrorAction Stop; "
        "$diskNum = ($img | Get-Disk).Number; "
        "$log += \"挂载磁盘号: $diskNum\"; "

        "$parts = Get-Partition -DiskNumber $diskNum | Where-Object { $_.Type -eq 'Basic' -and $_.Size -gt 10GB }; "
        "if (-not $parts) { Dismount-DiskImage -ImagePath $vhd; throw '未找到 Windows 分区' }; "
        "$part = $parts | Select-Object -First 1; "

        "$used = (Get-PSDrive -PSProvider FileSystem).Name; "
        "$letter = $null; "
        "foreach ($c in [char[]]('Z','Y','X','W','V','U','T','S','R','Q')) { "
        "  if ($c -notin $used) { $letter = $c; break } "
        "}; "
        "if (-not $letter) { Dismount-DiskImage -ImagePath $vhd; throw '无可用盘符' }; "

        "Set-Partition -InputObject $part -NewDriveLetter $letter -ErrorAction Stop; "
        "$drive = \"${letter}:\"; "
        "$log += \"分配盘符: $drive\"; "

        "if (-not (Test-Path \"$drive\\Windows\\System32\")) { "
        "  Remove-PartitionAccessPath -DiskNumber $diskNum -PartitionNumber $part.PartitionNumber -AccessPath \"$drive\\\" -ErrorAction SilentlyContinue; "
        "  Dismount-DiskImage -ImagePath $vhd; "
        "  throw '不是有效的 Windows 系统分区' "
        "}; "

        "$src = \"$env:SystemRoot\\System32\\DriverStore\\FileRepository\"; "
        "$dst = \"$drive\\Windows\\System32\\HostDriverStore\\FileRepository\"; "
        "if (-not (Test-Path $dst)) { New-Item -Path $dst -ItemType Directory -Force | Out-Null }; "
        "$log += '正在同步驱动文件 (robocopy)...'; "
        "& robocopy $src $dst /E /R:1 /W:1 /MT:32 /NDL /NJH /NJS /NC /NS | Out-Null; "
        "$log += '驱动文件同步完成'; "

        "$classPath = 'SYSTEM\\CurrentControlSet\\Control\\Class\\{4d36e968-e325-11ce-bfc1-08002be10318}'; "
        "$baseKey = [Microsoft.Win32.RegistryKey]::OpenBaseKey('LocalMachine', 'Registry64'); "
        "$classKey = $baseKey.OpenSubKey($classPath); "
        "if ($classKey) { "
        "  $guestRepo = \"$drive\\Windows\\System32\\HostDriverStore\\FileRepository\"; "
        "  foreach ($subName in $classKey.GetSubKeyNames()) { "
        "    $sub = $classKey.OpenSubKey($subName); "
        "    if (-not $sub) { continue }; "
        "    foreach ($promoName in @('CopyToVmWhenNewer','CopyToVmOverwrite','CopyToVmWhenNewerWow64','CopyToVmOverwriteWow64')) { "
        "      $targetDir = if ($promoName -like '*Wow64') { 'SysWOW64' } else { 'System32' }; "
        "      $promoKey = $sub.OpenSubKey($promoName); "
        "      if (-not $promoKey) { continue }; "
        "      foreach ($vn in $promoKey.GetValueNames()) { "
        "        $val = $promoKey.GetValue($vn); "
        "        $srcFile = $null; $tgtFile = $null; "
        "        if ($val -is [string[]]) { $srcFile = $val[0]; $tgtFile = if($val.Length -gt 1){$val[1]}else{$val[0]} } "
        "        elseif ($val -is [string]) { $srcFile = $val; $tgtFile = $val }; "
        "        if ($srcFile) { "
        "          $found = Get-ChildItem -Path $guestRepo -Filter $srcFile -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; "
        "          if ($found) { "
        "            $hostDst = \"$drive\\Windows\\$targetDir\"; "
        "            $linkPath = Join-Path $hostDst $tgtFile; "
        "            $guestTarget = $found.FullName -replace [regex]::Escape($drive), 'C:'; "
        "            if (Test-Path $linkPath) { Remove-Item $linkPath -Force }; "
        "            cmd /c mklink \"$linkPath\" \"$guestTarget\" 2>&1 | Out-Null "
        "          } "
        "        } "
        "      } "
        "    } "
        "  }; "
        "  $log += '注册表提升文件链接完成' "
        "}; "

        "$gpu = Get-CimInstance -Class Win32_VideoController | Select-Object -First 1; "
        "$manu = $gpu.AdapterCompatibility; "
        "if ($manu -like '*NVIDIA*') { "
        "  $log += '检测到 NVIDIA GPU，注入注册表...'; "
        "  $tmpReg = Join-Path $env:TEMP \"nvlddmkm_$(New-Guid).reg\"; "
        "  $sysHive = \"$drive\\Windows\\System32\\Config\\SYSTEM\"; "
        "  try { "
        "    reg unload 'HKLM\\OfflineSystem' 2>&1 | Out-Null; "
        "    reg export 'HKLM\\SYSTEM\\CurrentControlSet\\Services\\nvlddmkm' $tmpReg /y 2>&1 | Out-Null; "
        "    reg load 'HKLM\\OfflineSystem' $sysHive 2>&1 | Out-Null; "
        "    $content = Get-Content $tmpReg -Raw; "
        "    $content = $content -replace 'HKEY_LOCAL_MACHINE\\\\SYSTEM\\\\CurrentControlSet\\\\Services\\\\nvlddmkm', "
        "              'HKEY_LOCAL_MACHINE\\OfflineSystem\\ControlSet001\\Services\\nvlddmkm'; "
        "    $content = $content -replace 'DriverStore', 'HostDriverStore'; "
        "    Set-Content $tmpReg $content; "
        "    reg import $tmpReg 2>&1 | Out-Null; "
        "    $log += 'NVIDIA 注册表注入完成' "
        "  } catch { $log += \"NVIDIA 注册表注入失败: $_\" } "
        "  finally { "
        "    reg unload 'HKLM\\OfflineSystem' 2>&1 | Out-Null; "
        "    Remove-Item $tmpReg -Force -ErrorAction SilentlyContinue "
        "  }; "

        "  $gRepo = \"$drive\\Windows\\System32\\HostDriverStore\\FileRepository\"; "
        "  $s32 = \"$drive\\Windows\\System32\"; "
        "  $nvidiaLinks = @("
        "    @('nvidia-smi.exe','nvidia-smi.exe'), @('nvml_loader.dll','nvml.dll'), @('nvapi64.dll','nvapi64.dll'), "
        "    @('nvcuda_loader64.dll','nvcuda.dll'), @('OpenCL64.dll','OpenCL.dll'), "
        "    @('nvofapi64.dll','nvofapi64.dll'), @('nvEncodeAPI64.dll','nvEncodeAPI64.dll'), @('nvcuvid64.dll','nvcuvid.dll'), "
        "    @('vulkan-1-x64.dll','vulkan-1.dll'), @('_nvngx.dll','_nvngx.dll'), @('NvFBC64.dll','NvFBC64.dll') "
        "  ); "
        "  foreach ($pair in $nvidiaLinks) { "
        "    $f = Get-ChildItem -Path $gRepo -Filter $pair[0] -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; "
        "    if ($f) { "
        "      $lnk = Join-Path $s32 $pair[1]; "
        "      $tgt = $f.FullName -replace [regex]::Escape($drive), 'C:'; "
        "      if (Test-Path $lnk) { Remove-Item $lnk -Force }; "
        "      cmd /c mklink \"$lnk\" \"$tgt\" 2>&1 | Out-Null "
        "    } "
        "  }; "
        "  $log += 'NVIDIA 符号链接创建完成' "
        "}; "

        "if ($manu -like '*Intel*') { "
        "  $gRepo = \"$drive\\Windows\\System32\\HostDriverStore\\FileRepository\"; "
        "  $s32 = \"$drive\\Windows\\System32\"; "
        "  $intelLinks = @("
        "    @('vulkan-1-64.dll','vulkan-1.dll'), @('ze_loader.dll','ze_loader.dll'), "
        "    @('mfx_loader_dll_hw64.dll','libmfxhw64.dll'), @('vpl_dispatcher_64.dll','libvpl.dll') "
        "  ); "
        "  foreach ($pair in $intelLinks) { "
        "    $f = Get-ChildItem -Path $gRepo -Filter $pair[0] -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; "
        "    if ($f) { "
        "      $lnk = Join-Path $s32 $pair[1]; "
        "      $tgt = $f.FullName -replace [regex]::Escape($drive), 'C:'; "
        "      if (Test-Path $lnk) { Remove-Item $lnk -Force }; "
        "      cmd /c mklink \"$lnk\" \"$tgt\" 2>&1 | Out-Null "
        "    } "
        "  }; "
        "  $log += 'Intel GPU 符号链接创建完成' "
        "}; "

        "if ($manu -like '*AMD*' -or $manu -like '*Advanced*') { "
        "  $gRepo = \"$drive\\Windows\\System32\\HostDriverStore\\FileRepository\"; "
        "  $s32 = \"$drive\\Windows\\System32\"; "
        "  $amdLinks = @("
        "    @('atidxx64.dll','atidxx64.dll'), @('amdxx64.dll','amdxx64.dll'), "
        "    @('amdvlk64.dll','amdvlk64.dll'), @('amdvlk64.dll','vulkan-1.dll') "
        "  ); "
        "  foreach ($pair in $amdLinks) { "
        "    $f = Get-ChildItem -Path $gRepo -Filter $pair[0] -Recurse -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1; "
        "    if ($f) { "
        "      $lnk = Join-Path $s32 $pair[1]; "
        "      $tgt = $f.FullName -replace [regex]::Escape($drive), 'C:'; "
        "      if (Test-Path $lnk) { Remove-Item $lnk -Force }; "
        "      cmd /c mklink \"$lnk\" \"$tgt\" 2>&1 | Out-Null "
        "    } "
        "  }; "
        "  $log += 'AMD GPU 符号链接创建完成' "
        "}; "

        "Remove-PartitionAccessPath -DiskNumber $diskNum -PartitionNumber $part.PartitionNumber -AccessPath \"$drive\\\" -ErrorAction SilentlyContinue; "
        "Dismount-DiskImage -ImagePath $vhd -ErrorAction SilentlyContinue; "
        "$log += '磁盘已卸载'; "
        "$log -join \"`n\""
    ).arg(safeVmName);

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut)
        {
            callback(false, "操作超时");
            return;
        }
        if (exitCode != 0)
        {
            callback(false, stdErr.isEmpty() ? stdOut : stdErr);
            return;
        }
        callback(true, stdOut);
    }, 300000);
}

void HyperVManager::getDDADevicesAsync(const std::function<void(const QList<DDADeviceInfo>&, const QString&)>& callback)
{
    const QString cmd =
        "$results = @(); "
        "$vmDevices = @{}; "
        "Get-VM | ForEach-Object { "
        "  $vm = $_; "
        "  Get-VMAssignableDevice -VMName $vm.Name -ErrorAction SilentlyContinue | ForEach-Object { "
        "    $vmDevices[$_.LocationPath] = $vm.Name "
        "  } "
        "}; "
        "$devices = Get-PnpDevice -PresentOnly | Where-Object { "
        "  $_.InstanceId -like 'PCI\\*' -and $_.Class -in @('Display','Net','USB','AudioEndpoint','Media','SCSIAdapter') "
        "}; "
        "foreach ($dev in $devices) { "
        "  $locPaths = (Get-PnpDeviceProperty -InstanceId $dev.InstanceId -KeyName DEVPKEY_Device_LocationPaths -ErrorAction SilentlyContinue).Data; "
        "  $locPath = if($locPaths){$locPaths[0]}else{''}; "
        "  $assignedVm = if($locPath -and $vmDevices.ContainsKey($locPath)){$vmDevices[$locPath]}else{''}; "
        "  $results += [PSCustomObject]@{ "
        "    InstanceId=$dev.InstanceId; FriendlyName=$dev.FriendlyName; "
        "    DeviceClass=$dev.Class; LocationPath=$locPath; "
        "    Status=$dev.Status; AssignedVm=$assignedVm "
        "  } "
        "}; "
        "$results | ConvertTo-Json -Compress";

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        QList<DDADeviceInfo> devices;
        QJsonArray arr = parseJsonArray(stdOut);
        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            DDADeviceInfo info;
            info.instanceId = obj["InstanceId"].toString();
            info.friendlyName = obj["FriendlyName"].toString();
            info.deviceClass = obj["DeviceClass"].toString();
            info.locationPath = obj["LocationPath"].toString();
            info.status = obj["Status"].toString();
            info.assignedVm = obj["AssignedVm"].toString();
            devices.append(info);
        }
        callback(devices, {});
    }, 60000);
}

void HyperVManager::dismountHostDevice(const QString& instanceId, const QString& locationPath)
{
    const QString cmd = QString(
        "Disable-PnpDevice -InstanceId %1 -Confirm:$false -ErrorAction Stop; "
        "Dismount-VMHostAssignableDevice -Force -LocationPath %2 -ErrorAction Stop")
        .arg(quotePS(instanceId), quotePS(locationPath));
    runPowerShellAsync(cmd, instanceId, "卸载设备");
}

void HyperVManager::assignDeviceToVM(const QString& vmName, const QString& locationPath)
{
    const QString cmd = QString(
        "Set-VM -Name %1 -AutomaticStopAction TurnOff; "
        "Set-VM -GuestControlledCacheTypes $true -VMName %1; "
        "Add-VMAssignableDevice -LocationPath %2 -VMName %1")
        .arg(quotePS(vmName), quotePS(locationPath));
    runPowerShellAsync(cmd, vmName, "分配设备");
}

void HyperVManager::removeDeviceFromVM(const QString& vmName, const QString& locationPath)
{
    const QString cmd = QString("Remove-VMAssignableDevice -LocationPath %1 -VMName %2")
                            .arg(quotePS(locationPath), quotePS(vmName));
    runPowerShellAsync(cmd, vmName, "移除分配设备");
}

void HyperVManager::mountHostDevice(const QString& instanceId, const QString& locationPath)
{
    const QString cmd = QString(
        "Mount-VMHostAssignableDevice -LocationPath %1 -ErrorAction Stop; "
        "Enable-PnpDevice -InstanceId %2 -Confirm:$false -ErrorAction Stop")
        .arg(quotePS(locationPath), quotePS(instanceId));
    runPowerShellAsync(cmd, instanceId, "挂载设备到宿主机");
}

void HyperVManager::getHostInfoAsync(const std::function<void(const HyperVHostInfo&, const QString&)>& callback)
{
    const QString cmd =
        "$info = [PSCustomObject]@{ "
        "  HypervisorPresent = (Get-CimInstance Win32_ComputerSystem).HypervisorPresent; "
        "  IommuAvailable = $false; "
        "  SchedulerType = ''; "
        "  NumaSpanning = $false; "
        "  VmHost = ''; "
        "  SupportedVersions = '' "
        "}; "
        "try { $dg = Get-CimInstance -Namespace root/Microsoft/Windows/DeviceGuard -ClassName Win32_DeviceGuard -ErrorAction Stop; "
        "  $info.IommuAvailable = ($dg.AvailableSecurityProperties -contains 3) } catch {}; "
        "try { $evt = Get-WinEvent -FilterHashtable @{ProviderName='Microsoft-Windows-Hyper-V-Hypervisor';Id=2} -MaxEvents 1 -ErrorAction Stop; "
        "  $code = [int]$evt.Properties[0].Value; "
        "  switch($code){ 1{$info.SchedulerType='Classic'} 2{$info.SchedulerType='Classic'} "
        "  3{$info.SchedulerType='Core'} 4{$info.SchedulerType='Root'} "
        "  default{$info.SchedulerType='Unknown'} } } catch { $info.SchedulerType = 'Unknown' }; "
        "try { $info.NumaSpanning = (Get-VMHost).NumaSpanningEnabled } catch {}; "
        "try { $info.VmHost = (Get-VMHost).VirtualMachinePath } catch {}; "
        "try { $info.SupportedVersions = ((Get-VMHostSupportedVersion | Select-Object -ExpandProperty Version) -join ',') } catch {}; "
        "$info | ConvertTo-Json -Compress";

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        HyperVHostInfo info;
        if (timedOut || exitCode != 0)
        {
            callback(info, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            callback(info, "解析宿主机信息失败");
            return;
        }

        QJsonObject obj = doc.object();
        info.hypervisorPresent = obj["HypervisorPresent"].toBool();
        info.iommuAvailable = obj["IommuAvailable"].toBool();
        info.schedulerType = obj["SchedulerType"].toString();
        info.numaSpanningEnabled = obj["NumaSpanning"].toBool();
        info.vmHost = obj["VmHost"].toString();
        info.supportedVersions = obj["SupportedVersions"].toString();
        callback(info, {});
    });
}

void HyperVManager::getSupportedVersionsAsync(const std::function<void(const QStringList&, const QString&)>& callback)
{
    runPowerShellCallback("Get-VMHostSupportedVersion | Sort-Object Version -Descending | ForEach-Object { '{0}.{1}' -f $_.Version.Major,$_.Version.Minor }",
        [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            if (timedOut || exitCode != 0)
            {
                callback({}, formatPowerShellError(stdErr, stdOut, timedOut));
                return;
            }
            QStringList versions;
            for (const QString& line : stdOut.split('\n', Qt::SkipEmptyParts))
            {
                const QString v = line.trimmed();
                if (!v.isEmpty()) versions.append(v);
            }
            callback(versions, {});
        });
}

void HyperVManager::getIsolationSupportAsync(const std::function<void(bool, const QStringList&, const QString&)>& callback)
{
    runPowerShellCallback(
        "(Get-Command New-VM).Parameters.ContainsKey('GuestStateIsolationType')",
        [this, callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            if (timedOut || exitCode != 0 || stdOut.trimmed().toLower() != "true")
            {
                callback(false, {"Disabled"}, {});
                return;
            }
            runPowerShellCallback(
                "((Get-Command New-VM).Parameters['GuestStateIsolationType'].Attributes | Where-Object { $_.ValidValues }).ValidValues",
                [callback](int exitCode2, const QString& stdOut2, const QString&, bool timedOut2)
                {
                    QStringList types;
                    if (!timedOut2 && exitCode2 == 0)
                    {
                        for (const QString& line : stdOut2.split('\n', Qt::SkipEmptyParts))
                        {
                            const QString t = line.trimmed();
                            if (!t.isEmpty()) types.append(t);
                        }
                    }
                    if (types.isEmpty()) types.append("Disabled");
                    callback(true, types, {});
                });
        });
}

void HyperVManager::setNumaSpanningAsync(bool enabled, const std::function<void(bool, const QString&)>& callback)
{
    QString boolStr = enabled ? "$true" : "$false";
    QString cmd = QString("Set-VMHost -NumaSpanningEnabled %1").arg(boolStr);
    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback(false, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        callback(true, {});
    });
}

void HyperVManager::setSchedulerTypeAsync(const QString& type, const std::function<void(bool, const QString&)>& callback)
{
    QString cmd = QString("bcdedit /set hypervisorschedulertype %1").arg(type);
    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        if (timedOut || exitCode != 0)
        {
            callback(false, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }
        callback(true, {});
    });
}

void HyperVManager::getVMThumbnailAsync(const QString& vmName, int width, int height,
                                        const std::function<void(const QImage&, const QString&)>& callback)
{
    QString safeVmName = vmName;
    safeVmName.replace("'", "''");

    QString ps = QString(
        "$ns = 'root\\virtualization\\v2';"
        "$vm = Get-CimInstance -Namespace $ns -ClassName Msvm_ComputerSystem "
        "  -Filter \"ElementName='%1'\" | Select-Object -First 1;"
        "if (-not $vm) { Write-Error 'VM not found'; exit 1 };"
        "$vssd = Get-CimAssociatedInstance -InputObject $vm "
        "  -ResultClassName Msvm_VirtualSystemSettingData | "
        "  Where-Object { $_.VirtualSystemType -eq 'Microsoft:Hyper-V:System:Realized' } | "
        "  Select-Object -First 1;"
        "if (-not $vssd) { Write-Error 'VSSD not found'; exit 1 };"
        "$mgmt = Get-CimInstance -Namespace $ns -ClassName Msvm_VirtualSystemManagementService;"
        "$params = @{ TargetSystem = $vssd; WidthPixels = %2; HeightPixels = %3 };"
        "$result = Invoke-CimMethod -InputObject $mgmt "
        "  -MethodName GetVirtualSystemThumbnailImage -Arguments $params;"
        "if ($result.ReturnValue -eq 0 -and $result.ImageData) {"
        "  [Convert]::ToBase64String([byte[]]$result.ImageData)"
        "} else {"
        "  Write-Error \"Thumbnail failed: $($result.ReturnValue)\"; exit 1"
        "}"
    ).arg(safeVmName).arg(width).arg(height);

    runPowerShellCallback(ps,
        [callback, width, height](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            if (timedOut || exitCode != 0 || stdOut.isEmpty())
            {
                callback(QImage(), stdErr.isEmpty() ? "获取缩略图失败" : stdErr.trimmed());
                return;
            }

            QByteArray raw = QByteArray::fromBase64(stdOut.trimmed().toUtf8());
            int expectedSize = width * height * 2;
            if (raw.size() < expectedSize)
            {
                callback(QImage(), QString("数据大小不匹配: %1 vs %2").arg(raw.size()).arg(expectedSize));
                return;
            }

            QImage img(reinterpret_cast<const uchar*>(raw.constData()),
                       width, height, width * 2, QImage::Format_RGB16);
            callback(img.copy(), {});
        }, 5000);
}

void HyperVManager::getVMSummaryInfoAsync(const QString& vmName,
                                           const std::function<void(const VMSummaryInfo&, const QString&)>& callback)
{
    QString safeVmName = vmName;
    safeVmName.replace("'", "''");

    QString ps = QString(
        "$vm = Get-VM -Name '%1' -ErrorAction Stop;"
        "[PSCustomObject]@{"
        "  Name = $vm.Name;"
        "  State = [int]$vm.State;"
        "  CpuUsage = $vm.CPUUsage;"
        "  MemoryAssigned = [math]::Round($vm.MemoryAssigned / 1MB);"
        "  MemoryDemand = [math]::Round($vm.MemoryDemand / 1MB);"
        "  MemoryStartup = [math]::Round($vm.MemoryStartup / 1MB);"
        "  UptimeMs = $vm.Uptime.TotalMilliseconds"
        "} | ConvertTo-Json -Compress"
    ).arg(safeVmName);

    runPowerShellCallback(ps,
        [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
        {
            VMSummaryInfo info;
            if (timedOut || exitCode != 0 || stdOut.isEmpty())
            {
                callback(info, stdErr.isEmpty() ? "获取摘要信息失败" : stdErr.trimmed());
                return;
            }

            QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8());
            QJsonObject obj = doc.object();
            info.name = obj["Name"].toString();

            int stateVal = obj["State"].toInt();
            switch (stateVal)
            {
                case 2: info.state = "Running"; break;
                case 3: info.state = "Off"; break;
                case 6: info.state = "Saved"; break;
                case 9: info.state = "Paused"; break;
                case 32769: info.state = "Starting"; break;
                default: info.state = QString("Unknown(%1)").arg(stateVal); break;
            }

            info.cpuUsage = obj["CpuUsage"].toInt();
            info.memoryUsageMB = static_cast<qint64>(obj["MemoryAssigned"].toDouble());
            qint64 demand = static_cast<qint64>(obj["MemoryDemand"].toDouble());
            qint64 startup = static_cast<qint64>(obj["MemoryStartup"].toDouble());

            if (info.memoryUsageMB > 0 && demand > 0)
            {
                int usedPercent = static_cast<int>(demand * 100 / info.memoryUsageMB);
                info.memoryAvailablePercent = 100 - qBound(0, usedPercent, 100);
            }
            else if (startup > 0)
            {
                info.memoryUsageMB = startup;
                info.memoryAvailablePercent = -1;
            }

            info.uptimeMs = static_cast<qint64>(obj["UptimeMs"].toDouble());

            callback(info, {});
        }, 5000);
}

void HyperVManager::getVMProcessorAdvancedAsync(const QString& vmName,
                                                const std::function<void(const VMProcessorAdvancedSettings&, const QString&)>& callback)
{
    const QString cmd = QString(
        "Get-VMProcessor -VMName %1 | Select-Object Count, Reserve, Maximum, "
        "RelativeWeight, ExposeVirtualizationExtensions, "
        "@{N='CompatibilityForMigration';E={$_.CompatibilityForMigrationEnabled}}, "
        "@{N='HideHypervisor';E={$_.EnableHostResourceProtection}} "
        "| ConvertTo-Json -Compress").arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        VMProcessorAdvancedSettings settings;
        if (timedOut || exitCode != 0)
        {
            callback(settings, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            callback(settings, "解析处理器设置失败");
            return;
        }

        QJsonObject obj = doc.object();
        settings.count = obj["Count"].toInt(1);
        settings.reserve = obj["Reserve"].toInt(0);
        settings.limit = obj["Maximum"].toInt(100);
        settings.weight = obj["RelativeWeight"].toInt(100);
        settings.exposeVirtualizationExtensions = obj["ExposeVirtualizationExtensions"].toBool();
        settings.nestedVirtualization = settings.exposeVirtualizationExtensions;
        settings.hideHypervisor = obj["HideHypervisor"].toBool();
        callback(settings, {});
    });
}

void HyperVManager::applyVMProcessorAdvanced(const QString& vmName, const VMProcessorAdvancedSettings& settings)
{
    const QString cmd = QString(
        "Set-VMProcessor -VMName %1 -Count %2 -Reserve %3 -Maximum %4 -RelativeWeight %5 "
        "-ExposeVirtualizationExtensions $%6 -EnableHostResourceProtection $%7")
        .arg(quotePS(vmName))
        .arg(settings.count)
        .arg(settings.reserve)
        .arg(settings.limit)
        .arg(settings.weight)
        .arg(settings.exposeVirtualizationExtensions ? "true" : "false")
        .arg(settings.hideHypervisor ? "true" : "false");
    runPowerShellAsync(cmd, vmName, "应用处理器设置");
}

void HyperVManager::getVMMemoryAdvancedAsync(const QString& vmName,
                                             const std::function<void(const VMMemoryAdvancedSettings&, const QString&)>& callback)
{
    const QString cmd = QString(
        "Get-VMMemory -VMName %1 | Select-Object "
        "@{N='StartupMB';E={$_.Startup/1MB}}, "
        "DynamicMemoryEnabled, "
        "@{N='MinimumMB';E={$_.Minimum/1MB}}, "
        "@{N='MaximumMB';E={$_.Maximum/1MB}}, "
        "Buffer, Priority "
        "| ConvertTo-Json -Compress").arg(quotePS(vmName));

    runPowerShellCallback(cmd, [callback](int exitCode, const QString& stdOut, const QString& stdErr, bool timedOut)
    {
        VMMemoryAdvancedSettings settings;
        if (timedOut || exitCode != 0)
        {
            callback(settings, formatPowerShellError(stdErr, stdOut, timedOut));
            return;
        }

        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(stdOut.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        {
            callback(settings, "解析内存设置失败");
            return;
        }

        QJsonObject obj = doc.object();
        settings.startupMB = static_cast<qint64>(obj["StartupMB"].toDouble());
        settings.dynamicMemoryEnabled = obj["DynamicMemoryEnabled"].toBool();
        settings.minimumMB = static_cast<qint64>(obj["MinimumMB"].toDouble());
        settings.maximumMB = static_cast<qint64>(obj["MaximumMB"].toDouble());
        settings.buffer = obj["Buffer"].toInt(20);
        settings.weight = obj["Priority"].toInt(5000);
        callback(settings, {});
    });
}

void HyperVManager::applyVMMemoryAdvanced(const QString& vmName, const VMMemoryAdvancedSettings& settings)
{
    QString cmd;
    const QString safeName = quotePS(vmName);

    if (settings.dynamicMemoryEnabled)
    {
        cmd = QString("Set-VMMemory -VMName %1 -StartupBytes %2MB -DynamicMemoryEnabled $true "
                      "-MinimumBytes %3MB -MaximumBytes %4MB -Buffer %5 -Priority %6")
                  .arg(safeName).arg(settings.startupMB).arg(settings.minimumMB)
                  .arg(settings.maximumMB).arg(settings.buffer).arg(settings.weight);
    }
    else
    {
        cmd = QString("Set-VMMemory -VMName %1 -StartupBytes %2MB -DynamicMemoryEnabled $false")
                  .arg(safeName).arg(settings.startupMB);
    }

    runPowerShellAsync(cmd, vmName, "应用内存设置");
}

void HyperVManager::checkGpuStrategyAsync(const std::function<void(bool, const QString&)>& callback)
{
    QString cmd = R"(
[bool]((Test-Path 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\HyperV') -and
       ($k = Get-Item 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\HyperV' -EA 0) -and
       ('RequireSecureDeviceAssignment','RequireSupportedDeviceAssignment' |
        ForEach-Object { ($k.GetValue($_, $null) -ne $null) }) -notcontains $false)
)";
    runPowerShellCallback(cmd, [callback](int exitCode, const QString& out, const QString& err, bool timedOut) {
        if (timedOut) { callback(false, "超时"); return; }
        bool enabled = out.trimmed().toLower() == "true";
        callback(enabled, QString());
    });
}

void HyperVManager::setGpuStrategyAsync(bool enable, const std::function<void(bool, const QString&)>& callback)
{
    QString cmd;
    if (enable)
    {
        cmd = R"(
$path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\HyperV'
if (-not (Test-Path $path)) { New-Item -Path $path -Force | Out-Null }
Set-ItemProperty -Path $path -Name 'RequireSecureDeviceAssignment' -Value 0 -Type DWord
Set-ItemProperty -Path $path -Name 'RequireSupportedDeviceAssignment' -Value 0 -Type DWord
'OK'
)";
    }
    else
    {
        cmd = R"(
$path = 'HKLM:\SOFTWARE\Policies\Microsoft\Windows\HyperV'
if (Test-Path $path) {
    Remove-ItemProperty -Path $path -Name 'RequireSecureDeviceAssignment' -ErrorAction SilentlyContinue
    Remove-ItemProperty -Path $path -Name 'RequireSupportedDeviceAssignment' -ErrorAction SilentlyContinue
}
'OK'
)";
    }
    runPowerShellCallback(cmd, [callback](int exitCode, const QString& out, const QString& err, bool timedOut) {
        if (timedOut) { callback(false, "超时"); return; }
        if (exitCode != 0) { callback(false, err.trimmed()); return; }
        callback(true, QString());
    });
}

void HyperVManager::checkIsServerSystemAsync(const std::function<void(bool, bool, const QString&)>& callback)
{
    QThread *thread = QThread::create([this, callback]() {
        bool isServer = false;
        bool switchEnabled = true;

        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\ProductOptions",
                          0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            WCHAR buf[256];
            DWORD bufSize = sizeof(buf);
            if (RegQueryValueExW(hKey, L"ProductType", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(buf), &bufSize) == ERROR_SUCCESS)
            {
                QString productType = QString::fromWCharArray(buf).trimmed();
                isServer = (productType.compare("WinNT", Qt::CaseInsensitive) != 0);
            }
            RegCloseKey(hKey);
        }

        HKEY hKeyVer;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                          L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          0, KEY_READ, &hKeyVer) == ERROR_SUCCESS)
        {
            WCHAR buf[256];
            DWORD bufSize = sizeof(buf);
            if (RegQueryValueExW(hKeyVer, L"EditionID", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(buf), &bufSize) == ERROR_SUCCESS)
            {
                QString editionId = QString::fromWCharArray(buf).trimmed();
                QStringList forbidden = {
                    "Professional", "Core", "Enterprise",
                    "CoreSingleLanguage", "CoreCountrySpecific"
                };
                if (forbidden.contains(editionId, Qt::CaseInsensitive) ||
                    editionId.startsWith("Server", Qt::CaseInsensitive))
                {
                    switchEnabled = false;
                }
            }
            RegCloseKey(hKeyVer);
        }

        QMetaObject::invokeMethod(this, [callback, isServer, switchEnabled]() {
            callback(isServer, switchEnabled, QString());
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

static bool enablePrivilege(const wchar_t* privilegeName)
{
    HANDLE hToken;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
        return false;

    LUID luid;
    if (!LookupPrivilegeValueW(nullptr, privilegeName, &luid))
    {
        CloseHandle(hToken);
        return false;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
    CloseHandle(hToken);
    return ok;
}

static QString executePatch(int mode)
{
    QString tempDir = "C:\\temp";
    QString hiveFile = tempDir + "\\sys_mod_exec.hiv";
    QString backupFile = tempDir + "\\sys_bak_exec.hiv";

    CreateDirectoryW(tempDir.toStdWString().c_str(), nullptr);

    DeleteFileW(hiveFile.toStdWString().c_str());
    DeleteFileW(backupFile.toStdWString().c_str());

    bool p1 = enablePrivilege(SE_BACKUP_NAME);
    bool p2 = enablePrivilege(SE_RESTORE_NAME);
    enablePrivilege(L"SeTakeOwnershipPrivilege");

    if (!p1 || !p2)
        return "无法获取必要权限 (SeBackupPrivilege / SeRestorePrivilege)";

    HKEY hKey;
    const DWORD READ_CONTROL_FLAG = 0x00020000;
    LONG openRet = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM", 0,
                                  READ_CONTROL_FLAG, &hKey);
    if (openRet != 0)
        return QString("打开 SYSTEM 键失败，错误码: %1").arg(openRet);

    LONG saveRet = RegSaveKeyW(hKey, hiveFile.toStdWString().c_str(), nullptr);
    RegCloseKey(hKey);

    if (saveRet != 0)
        return QString("导出 SYSTEM Hive 失败，错误码: %1").arg(saveRet);

    WIN32_FILE_ATTRIBUTE_DATA fileInfo;
    if (!GetFileAttributesExW(hiveFile.toStdWString().c_str(), GetFileExInfoStandard, &fileInfo))
        return "无法验证导出文件";

    ULONGLONG fileSize = (static_cast<ULONGLONG>(fileInfo.nFileSizeHigh) << 32) | fileInfo.nFileSizeLow;
    if (fileSize < 5 * 1024 * 1024)
        return QString("导出文件不完整 (%1 KB)").arg(fileSize / 1024);

    QString targetType = (mode == 1) ? "ServerNT" : "WinNT";
    const wchar_t* tempKeyName = L"TEMP_OFFLINE_SYS_MOD";

    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, tempKeyName);

    if (RegLoadKeyW(HKEY_LOCAL_MACHINE, tempKeyName,
                    hiveFile.toStdWString().c_str()) != 0)
        return "加载离线 Hive 失败";

    bool patchOk = false;
    {
        int currentSet = 1;
        QString selectPath = QString("TEMP_OFFLINE_SYS_MOD\\Select");
        HKEY hKeySelect;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, selectPath.toStdWString().c_str(),
                          0, KEY_READ, &hKeySelect) == ERROR_SUCCESS)
        {
            DWORD data = 0, size = sizeof(DWORD);
            if (RegQueryValueExW(hKeySelect, L"Current", nullptr, nullptr,
                                 reinterpret_cast<LPBYTE>(&data), &size) == ERROR_SUCCESS)
                currentSet = static_cast<int>(data);
            RegCloseKey(hKeySelect);
        }

        QString setPath = QString("TEMP_OFFLINE_SYS_MOD\\ControlSet%1\\Control\\ProductOptions")
                          .arg(currentSet, 3, 10, QChar('0'));
        HKEY hKeyProd;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, setPath.toStdWString().c_str(),
                          0, KEY_ALL_ACCESS, &hKeyProd) == ERROR_SUCCESS)
        {
            std::wstring typeW = targetType.toStdWString();
            typeW.push_back(L'\0');
            RegSetValueExW(hKeyProd, L"ProductType", 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(typeW.c_str()),
                           static_cast<DWORD>(typeW.size() * sizeof(wchar_t)));

            RegDeleteValueW(hKeyProd, L"SubscriptionPf");

            RegDeleteValueW(hKeyProd, L"ProductSuite");
            BYTE emptyMultiSz[] = {0, 0, 0, 0};
            RegSetValueExW(hKeyProd, L"ProductSuite", 0, REG_MULTI_SZ,
                           emptyMultiSz, sizeof(emptyMultiSz));

            RegCloseKey(hKeyProd);
            RegFlushKey(HKEY_LOCAL_MACHINE);
            patchOk = true;
        }
    }

    RegUnLoadKeyW(HKEY_LOCAL_MACHINE, tempKeyName);

    if (!patchOk)
        return "离线修改注册表失败";

    LONG replaceRet = RegReplaceKeyW(HKEY_LOCAL_MACHINE, L"SYSTEM",
                                      hiveFile.toStdWString().c_str(),
                                      backupFile.toStdWString().c_str());

    if (replaceRet == 0)
        return "SUCCESS";
    else if (replaceRet == 5)
        return "替换失败：系统锁定了该操作 (Access Denied)，可能需要在安全模式下操作";
    else
        return QString("替换 SYSTEM Hive 失败，错误码: %1").arg(replaceRet);
}

void HyperVManager::switchSystemVersionAsync(bool toServer, const std::function<void(bool, const QString&)>& callback)
{
    QThread *thread = QThread::create([this, toServer, callback]() {
        QString result = executePatch(toServer ? 1 : 2);
        bool success = (result == "SUCCESS");
        QString msg = success ? "切换成功，请重启计算机以生效" : result;
        QMetaObject::invokeMethod(this, [callback, success, msg]() {
            callback(success, msg);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

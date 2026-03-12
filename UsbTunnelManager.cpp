#include "UsbTunnelManager.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

UsbTunnelManager* UsbTunnelManager::getInstance()
{
    static UsbTunnelManager instance;
    return &instance;
}

UsbTunnelManager::UsbTunnelManager(QObject *parent)
    : QObject(parent)
{
}

void UsbTunnelManager::refreshDevices()
{
    QProcess psProc;
    psProc.setProgram("powershell");
    psProc.setArguments({
        "-NoProfile", "-Command",
        "Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match '^USB\\\\VID_' } | "
        "Select-Object @{N='InstanceId';E={$_.InstanceId}}, "
        "@{N='Name';E={$_.FriendlyName}}, "
        "@{N='Status';E={$_.Status}}, "
        "@{N='Class';E={$_.Class}} | ConvertTo-Json -Compress"
    });
    psProc.start();

    _devices.clear();

    if (psProc.waitForStarted(3000) && psProc.waitForFinished(10000))
    {
        const QString output = QString::fromUtf8(psProc.readAllStandardOutput()).trimmed();
        QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8());
        QJsonArray arr;
        if (doc.isArray()) arr = doc.array();
        else if (doc.isObject()) arr.append(doc.object());

        static const QRegularExpression vidPidRe(R"(VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4}))");

        for (const QJsonValue& val : arr)
        {
            QJsonObject obj = val.toObject();
            UsbDeviceDisplay dev;
            dev.instanceId = obj["InstanceId"].toString();
            dev.description = obj["Name"].toString();
            dev.status = obj["Status"].toString();
            dev.deviceClass = obj["Class"].toString();

            auto m = vidPidRe.match(dev.instanceId);
            if (m.hasMatch())
                dev.vidPid = m.captured(1) + ":" + m.captured(2);

            if (!dev.instanceId.isEmpty())
                _devices.append(dev);
        }
    }

    emit devicesRefreshed();
}

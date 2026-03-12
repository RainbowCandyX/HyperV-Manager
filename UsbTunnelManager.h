#ifndef USBTUNNELMANAGER_H
#define USBTUNNELMANAGER_H

#include <QObject>
#include <QList>

struct UsbDeviceDisplay
{
    QString instanceId;
    QString description;
    QString vidPid;
    QString deviceClass;
    QString status;
};

class UsbTunnelManager : public QObject
{
    Q_OBJECT
public:
    static UsbTunnelManager* getInstance();

    void refreshDevices();

    QList<UsbDeviceDisplay> devices() const { return _devices; }

signals:
    void devicesRefreshed();

private:
    explicit UsbTunnelManager(QObject* parent = nullptr);

    QList<UsbDeviceDisplay> _devices;
};

#endif

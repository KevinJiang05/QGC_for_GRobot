/****************************************************************************
 *
 * DeepShark telemetry arrival tracker.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>

class Vehicle;

/// Reports whether attitude and depth telemetry has arrived from a vehicle at all.
///
/// The corresponding Facts start at 0 and only signal changes, so their values cannot tell
/// "level at the surface" from "nothing received yet". This does not judge how old the data is.
class DeepSharkTelemetryArrival : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* vehicle READ vehicle WRITE setVehicle NOTIFY vehicleChanged)
    Q_PROPERTY(bool attitudeReceived READ attitudeReceived NOTIFY attitudeReceivedChanged)
    Q_PROPERTY(bool depthReceived READ depthReceived NOTIFY depthReceivedChanged)

public:
    explicit DeepSharkTelemetryArrival(QObject* parent = nullptr);

    QObject* vehicle() const { return _vehicleObject; }
    void setVehicle(QObject* vehicle);

    bool attitudeReceived() const { return _attitudeReceived; }
    bool depthReceived() const { return _depthReceived; }

signals:
    void vehicleChanged();
    void attitudeReceivedChanged();
    void depthReceivedChanged();

private:
    void _setAttitudeReceived(bool received);
    void _setDepthReceived(bool received);

    QPointer<QObject> _vehicleObject;
    QPointer<Vehicle> _vehicle;
    bool _attitudeReceived = false;
    bool _depthReceived = false;
};

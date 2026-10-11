/****************************************************************************
 *
 * DeepShark telemetry arrival tracker.
 *
 ****************************************************************************/

#include "DeepSharkTelemetryArrival.h"

#include "Vehicle.h"
#include "VehicleLinkManager.h"

DeepSharkTelemetryArrival::DeepSharkTelemetryArrival(QObject* parent) : QObject(parent)
{
}

void DeepSharkTelemetryArrival::setVehicle(QObject* vehicle)
{
    if (_vehicleObject == vehicle) {
        return;
    }
    if (_vehicle) {
        disconnect(_vehicle, nullptr, this, nullptr);
        disconnect(_vehicle->vehicleLinkManager(), nullptr, this, nullptr);
    }
    _vehicleObject = vehicle;
    _vehicle = qobject_cast<Vehicle*>(vehicle);
    _setAttitudeReceived(false);
    _setDepthReceived(false);
    if (_vehicle) {
        // The vehicle prefers one kind of attitude and altitude message over another and remembers that
        // choice. Only it knows which message it actually used, so the arrival is taken from there and not
        // from the message stream.
        connect(_vehicle, &VehicleFactGroup::attitudeAccepted, this, [this]() { _setAttitudeReceived(true); });
        connect(_vehicle, &VehicleFactGroup::altitudeRelativeAccepted, this, [this]() { _setDepthReceived(true); });
        // What was received before a link loss says nothing about the vehicle after it.
        connect(_vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, [this](bool lost) {
            if (lost) {
                _setAttitudeReceived(false);
                _setDepthReceived(false);
            }
        });
    }
    emit vehicleChanged();
}

void DeepSharkTelemetryArrival::_setAttitudeReceived(bool received)
{
    if (_attitudeReceived != received) {
        _attitudeReceived = received;
        emit attitudeReceivedChanged();
    }
}

void DeepSharkTelemetryArrival::_setDepthReceived(bool received)
{
    if (_depthReceived != received) {
        _depthReceived = received;
        emit depthReceivedChanged();
    }
}

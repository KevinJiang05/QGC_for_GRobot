#pragma once

#include <QtQuick/QQuickItem>
#include <memory>

#include "BaseClasses/VehicleTestManualConnect.h"

class APMSensorsComponentController;

class APMSensorsSessionStatusLog : public QQuickItem
{
    Q_OBJECT

public:
    QStringList lines;

    Q_INVOKABLE void append(const QString& text) { lines.append(text); }
};

class APMSensorsSessionTest : public VehicleTestManualConnect
{
    Q_OBJECT

protected slots:
    void init() override;
    void cleanup() override;

private slots:
    void _missingStartCanEndLocalWait_data();
    void _missingStartCanEndLocalWait();
    void _cancelWithoutResponseCanEndLocalWait();
    void _startAcceptedAfterCancelIsUnknown_data();
    void _startAcceptedAfterCancelIsUnknown();
    void _slowCalibrationCanFinishAfterTimeout();
    void _canStartAgainAfterLocalExit();
    void _lateAccelResultDoesNotFinishPressureSession();
    void _destructionRestoresOriginalMonitoring_data();
    void _destructionRestoresOriginalMonitoring();
    void _vehicleDestroyedBeforeController();
    void _armedOverrideStillRejected();

private:
    std::unique_ptr<APMSensorsComponentController> _createController();
    void _startCalibration(APMSensorsComponentController& controller, int kind);
    void _sendAck(MAV_RESULT result);

    std::unique_ptr<QQuickItem> _next;
    std::unique_ptr<QQuickItem> _cancel;
    std::unique_ptr<QQuickItem> _progress;
    std::unique_ptr<QQuickItem> _help;
    std::unique_ptr<APMSensorsSessionStatusLog> _log;
    QMetaObject::Connection _wireFilter;
    bool _dropCalibrationFrames = true;
    int _droppedCalibrationFrames = 0;
};

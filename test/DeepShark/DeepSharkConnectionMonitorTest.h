#pragma once
#include "UnitTest.h"
class DeepSharkConnectionMonitor;

class DeepSharkConnectionMonitorTest : public UnitTest
{
    Q_OBJECT
private slots:
    void _startupRequiresStableLinks();
    void _videoAndHeartbeatFaults();
    void _recoveryAndAcknowledgement();
    void _manualActionsAndMasterSwitch();
    void _frameProgressSurvivesReconnect();
    void _settingsRoundTrip();
    void _vehicleSignals();
    void _existingAlarmSurvivesRearm();
    void _steadyStateDoesNotRenotify();
    void _deadCameraDoesNotBlockFlightMonitoring();
    void _flightIsMonitoredWithoutVideo();
    void _alarmKeepsSourcesThatNeverRecovered();
    void _disablingAFaultedCameraEndsItsAlarm_data();
    void _disablingAFaultedCameraEndsItsAlarm();
    void _switchedOffCameraIsNotReportedAsRecovered();
    void _qmlControlsLoad();
private:
    void _prepare(DeepSharkConnectionMonitor &monitor);
    void _fresh(DeepSharkConnectionMonitor &monitor, qint64 now);
};

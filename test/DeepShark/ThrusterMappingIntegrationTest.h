#pragma once

#include <QtCore/QMetaObject>

#include "BaseClasses/VehicleTestManualConnect.h"

class ThrusterMappingIntegrationTest : public VehicleTestManualConnect
{
    Q_OBJECT

private slots:
    void cleanup() override;
    void _abortWaitsForPendingPwmAck();
    void _parameterRejectionCannotConfirmDisable();
    void _missingNeutralAckKeepsQmlRecoveryJournal();
    void _qmlModesAndDisconnectedExport();
    void _safeEndWaitsForPendingArmAndDisarms_data();
    void _safeEndWaitsForPendingArmAndDisarms();
    void _freshBackupOverridesStaleCache();
    void _backupReadFailureDoesNotCreateRecoveryJournal();
    void _wiringRecordsAreScopedToVehicleUid();
    void _unknownUidRecordsStayInSession();
    void _functionSaveReadsBackAndRejectsStaleValues_data();
    void _functionSaveReadsBackAndRejectsStaleValues();
    void _functionSaveConfirmationInterlocks_data();
    void _functionSaveConfirmationInterlocks();
    void _desktopDialogTabs();

private:
    enum ServoAckPolicy
    {
        HoldServoAck,
        AcceptServoAck
    };

    void _connectFixture(quint64 uid = 0);
    void _filterMockResponse(LinkInterface* link, const QByteArray& bytes);

    QMetaObject::Connection _responseFilter;
    ServoAckPolicy _servoAckPolicy = AcceptServoAck;
    quint64 _fixtureUid = 0;
    bool _holdArmingUpdates = false;
    bool _holdArmingHeartbeats = false;
};

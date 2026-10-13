#pragma once

#include <QtCore/QList>
#include <QtCore/QMetaObject>
#include <QtCore/QPair>
#include <array>

#include "BaseClasses/VehicleTestManualConnect.h"

class ThrusterMappingIntegrationTest : public VehicleTestManualConnect
{
    Q_OBJECT

private slots:
    void cleanup() override;
    void _abortWaitsForPendingPwmAck();
    void _parameterRejectionCannotConfirmDisable();
    void _missingNeutralAckKeepsQmlRecoveryJournal();
    void _linkLossAndReturnSendNeutralAgain();
    void _linkReturnLeavesAReassignedOutputAlone();
    void _linkLossLeavesALiveReassignedOutputAlone();
    void _delayedReissueChecksTheOutputAgain();
    void _lateTestReplyIsNotTakenForNeutral();
    void _testNeedsOutputReports();
    void _neutralWithoutOutputReportsKeepsTheOutput();
    void _testPwmStillOnItsWayIsNotClearedByNeutralReports();
    void _qmlModesAndDisconnectedExport();
    void _safeEndWaitsForPendingArmAndDisarms_data();
    void _safeEndWaitsForPendingArmAndDisarms();
    void _freshBackupOverridesStaleCache();
    void _backupReadFailureDoesNotCreateRecoveryJournal();
    void _unfinishedRecoveryIsAnnouncedOnConnect();
    void _recoveryJournalIsStoredBeforeDisable();
    void _recordWithoutSerialNumberNeedsManualConfirmation();
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
    void _forwardMockResponse(LinkInterface* link, const QByteArray& bytes);
    void _applyServoCommands(const QByteArray& bytes);
    void _releaseHeldServoCommands();
    void _reportMockOutputs(LinkInterface* link);
    void _deliverOutputReport(LinkInterface* link, quint32 timeUsec, const std::array<int, 16>& outputs);

    QMetaObject::Connection _responseFilter;
    QMetaObject::Connection _commandWatch;
    ServoAckPolicy _servoAckPolicy = AcceptServoAck;
    // Stock MockLink has no outputs. These hold what a vehicle would: the PWM of the last servo command that
    // reached each output. They are reported with the mock's own traffic, as a vehicle reports its outputs.
    std::array<int, 16> _mockOutputs{};
    quint32 _mockOutputTimeUsec = 0;
    int _mockOutputReportsSent = 0;
    // Faults on the way to the outputs are chosen by the PWM of the command, so that which command is
    // affected does not depend on when the test gets to run.
    int _lostServoPwm = -1;  ///< Commands with this PWM never reach the output
    int _heldServoPwm = -1;  ///< Commands with this PWM reach it only when they are released
    QList<QPair<int, int>> _heldServoCommands;
    bool _outputsReported = true;
    bool _outputClockRuns = true;  ///< Whether the time in the reports moves on
    quint64 _fixtureUid = 0;
    bool _holdArmingUpdates = false;
    bool _holdArmingHeartbeats = false;
};

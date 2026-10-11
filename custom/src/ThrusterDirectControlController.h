#pragma once

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include <QtCore/QVariant>

class Fact;
class Vehicle;

class ThrusterDirectControlController : public QObject
{
    Q_OBJECT
    Q_MOC_INCLUDE("Fact.h")
    Q_MOC_INCLUDE("Vehicle.h")
    Q_PROPERTY(Vehicle* vehicle READ vehicle NOTIFY vehicleChanged)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool functionSaveBusy READ functionSaveBusy NOTIFY stateChanged)
    Q_PROPERTY(int output READ output NOTIFY detailsChanged)
    Q_PROPERTY(int pendingOutput READ pendingOutput NOTIFY detailsChanged)
    Q_PROPERTY(int originalFunction READ originalFunction NOTIFY detailsChanged)
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY detailsChanged)

public:
    enum State
    {
        Idle,
        WaitingBackup,
        WaitingDisable,
        DisableSettling,
        WaitingTestAck,
        Testing,
        WaitingNeutralAck,
        WaitingRestore,
        SavingFunction,
        RecoveryNeeded,
    };
    Q_ENUM(State)

    explicit ThrusterDirectControlController(QObject* parent = nullptr);

    State state() const { return _state; }

    QString stateName() const;

    bool busy() const { return _state != Idle && _state != RecoveryNeeded; }

    bool functionSaveBusy() const { return _state == SavingFunction; }

    int output() const { return _output; }

    int pendingOutput() const { return _pendingOutput; }

    int originalFunction() const { return _originalFunction; }

    QString failureReason() const { return _failureReason; }

    Vehicle* vehicle() const;

    Q_INVOKABLE bool bindVehicle(Vehicle* vehicle);
    Q_INVOKABLE Fact* getParameterFact(int componentId, const QString& name, bool reportMissing = false);
    Q_INVOKABLE bool parameterExists(int componentId, const QString& name) const;
    Q_INVOKABLE bool beginTest(int output, int originalFunction, int testPwm, int neutralPwm, int testDurationMs);
    Q_INVOKABLE bool beginRecovery(int output, int originalFunction, const QString& reason, int neutralPwm = 1500);
    Q_INVOKABLE bool beginFunctionSave(int output, int expectedOriginal, int newFunction);
    Q_INVOKABLE void abort(const QString& reason);
    Q_INVOKABLE void finishTest();
    Q_INVOKABLE bool reset();

    /// Operator notice for an output test that an earlier run left unfinished; empty when no record is pending.
    static QString pendingRecoveryNotice();

#ifdef QGC_UNITTEST_BUILD
    void reportParameterUnavailable(const QString& reason);
    void setTimingsForTest(int settleMs, int commandTimeoutMs, int parameterTimeoutMs);
    void observeParameterState(double rawValue, bool pendingWrites);
    void handleCommandResult(int ackResult, int failureCode = 0);
    int unconfirmedOutputForTest() const { return _unconfirmedOutput; }
#endif

signals:
    void vehicleChanged();
    void stateChanged();
    void detailsChanged();
    void functionWriteRequested(int value);
    void servoPwmRequested(int output, int pwm);
    void originalFunctionConfirmed(int output, int originalFunction);
    void testRejected(const QString& reason);
    void recoveryCompleted(int output, const QString& failureReason);
    void recoveryRequired(const QString& reason);
    /// A neutral PWM was sent again for an output whose neutral was never confirmed. Nothing confirms this one either.
    void neutralReissued(int output);
    /// The link returned, but the output could not be confirmed as still disabled by this session, so no
    /// neutral was sent on its own.
    void neutralReissueSkipped(int output);
    void functionSaveFinished(int output, bool success, const QString& message);

private slots:
    void _settleFinished();
    void _testDurationFinished();
    void _commandTimedOut();
    void _parameterTimedOut();

private:
    void _setState(State state);
    void _startParameterWait(int expectedValue, int timeoutMs);
    void _writeFunction(int value);
    void _requestParameterRead();
    void _parameterConfirmed(double value);
    void _parameterFailed(const QString& reason);
    bool _functionSaveAvailable() const;
    void _functionSaveReadConfirmed(double value);
    void _finishFunctionSave(bool success, const QString& message);
    void _rejectBeforeWrite(const QString& reason);
    void _sendPwm(int pwm);
    void _handleCommandResult(int ackResult, int failureCode, quint64 token);
    void _vehicleUnavailable();
    bool _vehicleUsable(bool requireDisarmed) const;
    void _stopParameterWait();
    // Writes the original function back without sending neutral; only valid before a test PWM was sent
    // or after neutral was confirmed, which is why it is not exposed to QML.
    void _requestRestore(const QString& reason);
    void _beginNeutral();
    void _retryNeutralOrRequireRecovery(const QString& reason);
    void _sendUnconfirmedNeutral();
    void _checkOutputBeforeReissue();
    void _outputCheckedForReissue(bool stillDisabled);
    void _cancelReissue();
    void _forgetUnconfirmedNeutral();
    bool _servoCommandOrParameterWritePending() const;
    void _requireRecovery(const QString& reason);
    void _completeRecovery();
    void _stopTimers();
    void _clearSession();
    static bool _validPwm(int pwm);

    enum ParameterOperation
    {
        NoParameterOperation,
        WritingParameter,
        ReadingParameter
    };

    QPointer<Vehicle> _vehicle;
    QPointer<Fact> _functionFact;
    QPointer<Fact> _reissueCheckFact; ///< Function parameter being read back before a neutral is sent on a link return
    QMetaObject::Connection _reissueCheckUpdate;
    QVariant _reissueCheckValue; ///< What the vehicle reported for it during that read
    bool _vehicleBound = false;
    bool _manualAdapter = false;
    bool _stopRequested = false;
    bool _recovering = false;
    bool _commandPending = false;
    bool _functionSaveWriteStarted = false;
    quint64 _commandToken = 0;
    ParameterOperation _parameterOperation = NoParameterOperation;
    State _state = Idle;
    int _sessionOutput = -1;
    int _output = -1;
    int _pendingOutput = -1;
    int _originalFunction = -1;
    int _newFunction = -1;
    int _testPwm = 1500;
    int _neutralPwm = 1500;
    int _testDurationMs = 1000;
    int _expectedParameterValue = -1;
    int _neutralAttemptsLeft = 0;
    // A command reply names the command only. When an earlier PWM command for this output ended without
    // a reply, its late reply could be handed to a pending neutral, so one more accepted neutral is needed.
    int _neutralAcksNeeded = 1;
    // Output that may still hold a test PWM after the session fell back to manual recovery.
    int _unconfirmedOutput = -1;
    int _unconfirmedNeutralPwm = 1500;
    int _reissueChecksLeft = 0;
    QString _failureReason;

    int _settleMs = 300;
    int _commandTimeoutMs = 4500;
    int _parameterTimeoutMs = 8000;
    int _restoreParameterTimeoutMs = 12000;

    QTimer _settleTimer;
    QTimer _testDurationTimer;
    QTimer _commandTimer;
    QTimer _parameterTimeoutTimer;
    QTimer _reissueTimer;
};

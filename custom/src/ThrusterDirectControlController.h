#pragma once

#include <QtCore/QObject>
#include <QtCore/QTimer>

class ThrusterDirectControlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(int output READ output NOTIFY detailsChanged)
    Q_PROPERTY(int pendingOutput READ pendingOutput NOTIFY detailsChanged)
    Q_PROPERTY(int originalFunction READ originalFunction NOTIFY detailsChanged)
    Q_PROPERTY(QString failureReason READ failureReason NOTIFY detailsChanged)

public:
    enum State {
        Idle,
        WaitingDisable,
        DisableSettling,
        WaitingTestAck,
        Testing,
        WaitingNeutralAck,
        WaitingRestore,
        RecoveryNeeded,
    };
    Q_ENUM(State)

    explicit ThrusterDirectControlController(QObject *parent = nullptr);

    State state() const { return _state; }
    QString stateName() const;
    bool busy() const { return _state != Idle && _state != RecoveryNeeded; }
    int output() const { return _output; }
    int pendingOutput() const { return _pendingOutput; }
    int originalFunction() const { return _originalFunction; }
    QString failureReason() const { return _failureReason; }

    Q_INVOKABLE bool beginTest(int output, int originalFunction, int testPwm, int neutralPwm, int testDurationMs);
    Q_INVOKABLE bool beginRecovery(int output, int originalFunction, const QString &reason);
    Q_INVOKABLE void observeParameterState(double rawValue, bool pendingWrites);
    Q_INVOKABLE void handleCommandResult(int ackResult);
    Q_INVOKABLE void abort(const QString &reason);
    Q_INVOKABLE void finishTest();
    Q_INVOKABLE void requestRestore(const QString &reason);
    Q_INVOKABLE void reportParameterUnavailable(const QString &reason);
    Q_INVOKABLE bool reset();

#ifdef QGC_UNITTEST_BUILD
    void setTimingsForTest(int settleMs, int commandTimeoutMs, int parameterTimeoutMs);
#endif

signals:
    void stateChanged();
    void detailsChanged();
    void functionWriteRequested(int value);
    void parameterCheckRequested(int expectedValue);
    void servoPwmRequested(int output, int pwm);
    void recoveryCompleted(int output, const QString &failureReason);
    void recoveryRequired(const QString &reason);

private slots:
    void _settleFinished();
    void _testDurationFinished();
    void _commandTimedOut();
    void _parameterTimedOut();

private:
    void _setState(State state);
    void _startParameterWait(int expectedValue, int timeoutMs);
    void _stopParameterWait();
    void _beginNeutral();
    void _requireRecovery(const QString &reason);
    void _completeRecovery();
    void _stopTimers();
    void _clearSession();
    static bool _validPwm(int pwm);

    State _state = Idle;
    int _sessionOutput = -1;
    int _output = -1;
    int _pendingOutput = -1;
    int _originalFunction = -1;
    int _testPwm = 1500;
    int _neutralPwm = 1500;
    int _testDurationMs = 1000;
    int _expectedParameterValue = -1;
    QString _failureReason;

    int _settleMs = 300;
    int _commandTimeoutMs = 4500;
    int _parameterTimeoutMs = 8000;
    int _restoreParameterTimeoutMs = 12000;

    QTimer _settleTimer;
    QTimer _testDurationTimer;
    QTimer _commandTimer;
    QTimer _parameterPollTimer;
    QTimer _parameterTimeoutTimer;
};

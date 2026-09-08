#include "ThrusterDirectControlController.h"

#include <QtCore/QtMath>

#include <cmath>

ThrusterDirectControlController::ThrusterDirectControlController(QObject *parent)
    : QObject(parent)
{
    _settleTimer.setSingleShot(true);
    _testDurationTimer.setSingleShot(true);
    _commandTimer.setSingleShot(true);
    _parameterTimeoutTimer.setSingleShot(true);
    _parameterPollTimer.setInterval(100);

    connect(&_settleTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_settleFinished);
    connect(&_testDurationTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_testDurationFinished);
    connect(&_commandTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_commandTimedOut);
    connect(&_parameterTimeoutTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_parameterTimedOut);
    connect(&_parameterPollTimer, &QTimer::timeout, this, [this]() {
        emit parameterCheckRequested(_expectedParameterValue);
    });
}

QString ThrusterDirectControlController::stateName() const
{
    switch (_state) {
    case WaitingDisable: return QStringLiteral("waitingDisable");
    case DisableSettling: return QStringLiteral("disableSettling");
    case WaitingTestAck: return QStringLiteral("waitingTestAck");
    case Testing: return QStringLiteral("testing");
    case WaitingNeutralAck: return QStringLiteral("waitingNeutralAck");
    case WaitingRestore: return QStringLiteral("waitingRestore");
    case RecoveryNeeded: return QStringLiteral("recoveryNeeded");
    case Idle:
    default: return QStringLiteral("idle");
    }
}

bool ThrusterDirectControlController::beginTest(int output, int originalFunction, int testPwm, int neutralPwm, int testDurationMs)
{
    if (_state != Idle || output < 1 || output > 16 || originalFunction < 0
        || !_validPwm(testPwm) || !_validPwm(neutralPwm) || testDurationMs < 1 || testDurationMs > 5000) {
        return false;
    }

    _sessionOutput = output;
    _pendingOutput = output;
    _output = -1;
    _originalFunction = originalFunction;
    _testPwm = testPwm;
    _neutralPwm = neutralPwm;
    _testDurationMs = testDurationMs;
    _failureReason.clear();
    emit detailsChanged();

    _setState(WaitingDisable);
    _startParameterWait(0, _parameterTimeoutMs);
    emit functionWriteRequested(0);
    return true;
}

bool ThrusterDirectControlController::beginRecovery(int output, int originalFunction, const QString &reason)
{
    if (busy() || output < 1 || output > 16 || originalFunction < 0) {
        return false;
    }

    _stopTimers();
    _sessionOutput = output;
    _output = -1;
    _pendingOutput = -1;
    _originalFunction = originalFunction;
    _failureReason = reason;
    emit detailsChanged();

    _setState(WaitingRestore);
    _startParameterWait(_originalFunction, _restoreParameterTimeoutMs);
    emit functionWriteRequested(_originalFunction);
    return true;
}

void ThrusterDirectControlController::observeParameterState(double rawValue, bool pendingWrites)
{
    if ((_state != WaitingDisable && _state != WaitingRestore) || pendingWrites || !std::isfinite(rawValue)) {
        return;
    }

    if (qAbs(rawValue - static_cast<double>(_expectedParameterValue)) > 0.0001) {
        return;
    }

    _stopParameterWait();
    if (_state == WaitingDisable) {
        _setState(DisableSettling);
        _settleTimer.start(_settleMs);
    } else {
        _completeRecovery();
    }
}

void ThrusterDirectControlController::handleCommandResult(int ackResult)
{
    if (_state == WaitingTestAck) {
        _commandTimer.stop();
        if (ackResult == 0) {
            _setState(Testing);
            _testDurationTimer.start(_testDurationMs);
        } else {
            requestRestore(tr("飞控拒绝测试 PWM 指令"));
        }
        return;
    }

    if (_state == WaitingNeutralAck) {
        _commandTimer.stop();
        requestRestore(ackResult == 0 ? QString() : tr("回中指令被拒绝"));
    }
}

void ThrusterDirectControlController::abort(const QString &reason)
{
    if (!busy()) {
        return;
    }

    if (!reason.isEmpty()) {
        _failureReason = reason;
        emit detailsChanged();
    }

    if (_state == Testing || _state == WaitingTestAck) {
        _beginNeutral();
    } else if (_state != WaitingNeutralAck && _state != WaitingRestore) {
        requestRestore(reason);
    }
}

void ThrusterDirectControlController::finishTest()
{
    if (_state == Testing) {
        _beginNeutral();
    }
}

void ThrusterDirectControlController::requestRestore(const QString &reason)
{
    _settleTimer.stop();
    _testDurationTimer.stop();
    _commandTimer.stop();
    _stopParameterWait();

    if (!reason.isEmpty()) {
        _failureReason = reason;
    }
    _output = -1;
    _pendingOutput = -1;
    emit detailsChanged();

    if (_originalFunction < 0) {
        _requireRecovery(_failureReason.isEmpty() ? tr("参数对象不可用") : _failureReason);
        return;
    }

    _setState(WaitingRestore);
    _startParameterWait(_originalFunction, _restoreParameterTimeoutMs);
    emit functionWriteRequested(_originalFunction);
}

void ThrusterDirectControlController::reportParameterUnavailable(const QString &reason)
{
    if (_state == WaitingRestore) {
        _requireRecovery(reason);
    } else if (busy()) {
        requestRestore(reason);
    }
}

bool ThrusterDirectControlController::reset()
{
    if (busy()) {
        return false;
    }

    _stopTimers();
    _clearSession();
    _setState(Idle);
    return true;
}

#ifdef QGC_UNITTEST_BUILD
void ThrusterDirectControlController::setTimingsForTest(int settleMs, int commandTimeoutMs, int parameterTimeoutMs)
{
    _settleMs = qMax(1, settleMs);
    _commandTimeoutMs = qMax(1, commandTimeoutMs);
    _parameterTimeoutMs = qMax(1, parameterTimeoutMs);
    _restoreParameterTimeoutMs = _parameterTimeoutMs;
    _parameterPollTimer.setInterval(qMax(1, qMin(10, parameterTimeoutMs / 2)));
}
#endif

void ThrusterDirectControlController::_settleFinished()
{
    if (_state != DisableSettling || _pendingOutput < 1) {
        requestRestore(tr("载具连接或直测状态发生变化"));
        return;
    }

    _output = _pendingOutput;
    _pendingOutput = -1;
    emit detailsChanged();
    _setState(WaitingTestAck);
    emit servoPwmRequested(_output, _testPwm);
    if (_state == WaitingTestAck) {
        _commandTimer.start(_commandTimeoutMs);
    }
}

void ThrusterDirectControlController::_testDurationFinished()
{
    finishTest();
}

void ThrusterDirectControlController::_commandTimedOut()
{
    if (_state == WaitingTestAck) {
        _failureReason = tr("测试 PWM 指令无回执");
        emit detailsChanged();
        _beginNeutral();
    } else if (_state == WaitingNeutralAck) {
        requestRestore(tr("回中指令无回执"));
    }
}

void ThrusterDirectControlController::_parameterTimedOut()
{
    _parameterPollTimer.stop();
    if (_state == WaitingDisable) {
        requestRestore(tr("等待 Disabled 写入确认超时"));
    } else if (_state == WaitingRestore) {
        _requireRecovery(tr("等待原功能参数写回确认超时"));
    }
}

void ThrusterDirectControlController::_setState(State state)
{
    if (_state == state) {
        return;
    }
    _state = state;
    emit stateChanged();
}

void ThrusterDirectControlController::_startParameterWait(int expectedValue, int timeoutMs)
{
    _expectedParameterValue = expectedValue;
    _parameterPollTimer.start();
    _parameterTimeoutTimer.start(timeoutMs);
}

void ThrusterDirectControlController::_stopParameterWait()
{
    _parameterPollTimer.stop();
    _parameterTimeoutTimer.stop();
}

void ThrusterDirectControlController::_beginNeutral()
{
    if (_output < 1) {
        requestRestore(tr("测试通道状态无效"));
        return;
    }

    _testDurationTimer.stop();
    _commandTimer.stop();
    _setState(WaitingNeutralAck);
    emit servoPwmRequested(_output, _neutralPwm);
    if (_state == WaitingNeutralAck) {
        _commandTimer.start(_commandTimeoutMs);
    }
}

void ThrusterDirectControlController::_requireRecovery(const QString &reason)
{
    _stopTimers();
    _failureReason = reason;
    _output = -1;
    _pendingOutput = -1;
    emit detailsChanged();
    _setState(RecoveryNeeded);
    emit recoveryRequired(reason);
}

void ThrusterDirectControlController::_completeRecovery()
{
    const int output = _sessionOutput;
    const QString failureReason = _failureReason;
    _stopTimers();
    _clearSession();
    _setState(Idle);
    emit recoveryCompleted(output, failureReason);
}

void ThrusterDirectControlController::_stopTimers()
{
    _settleTimer.stop();
    _testDurationTimer.stop();
    _commandTimer.stop();
    _stopParameterWait();
}

void ThrusterDirectControlController::_clearSession()
{
    _sessionOutput = -1;
    _output = -1;
    _pendingOutput = -1;
    _originalFunction = -1;
    _testPwm = 1500;
    _neutralPwm = 1500;
    _testDurationMs = 1000;
    _expectedParameterValue = -1;
    _failureReason.clear();
    emit detailsChanged();
}

bool ThrusterDirectControlController::_validPwm(int pwm)
{
    return pwm >= 800 && pwm <= 2200;
}

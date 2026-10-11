#include "ThrusterDirectControlController.h"

#include <QtCore/QSettings>
#include <QtCore/QtMath>
#include <cmath>
#include <limits>

#include "Fact.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "Vehicle.h"
#include "VehicleLinkManager.h"

namespace {

// DO_SET_SERVO is sent once by the vehicle's command queue, and a disabled output holds its last PWM
// with no timeout on the vehicle. A lost neutral therefore has to be repeated from here. This is the
// number of neutrals a session may send while it waits for neutral to be confirmed, whatever the reason
// for sending another one. The best-effort neutrals around a link loss are not counted here.
constexpr int kNeutralSendAttempts = 4;
constexpr int kReissueChecks = 8;
constexpr int kReissueCheckIntervalMs = 500;

// The vehicle owns callbacks until its command queue has finished or is destroyed.
// Closing the dialog therefore cannot invalidate the callback's data pointer.
class ServoCommandContext : public QObject
{
public:
    ServoCommandContext(Vehicle* vehicle, ThrusterDirectControlController* controller, quint64 token)
        : QObject(vehicle), controller(controller), token(token)
    {}

    QPointer<ThrusterDirectControlController> controller;
    quint64 token;
};

}  // namespace

ThrusterDirectControlController::ThrusterDirectControlController(QObject* parent) : QObject(parent)
{
    _settleTimer.setSingleShot(true);
    _testDurationTimer.setSingleShot(true);
    _commandTimer.setSingleShot(true);
    _parameterTimeoutTimer.setSingleShot(true);
    _reissueTimer.setSingleShot(true);

    connect(&_settleTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_settleFinished);
    connect(&_testDurationTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_testDurationFinished);
    connect(&_commandTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_commandTimedOut);
    connect(&_parameterTimeoutTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_parameterTimedOut);
    connect(&_reissueTimer, &QTimer::timeout, this, &ThrusterDirectControlController::_checkOutputBeforeReissue);
}

Vehicle* ThrusterDirectControlController::vehicle() const
{
    return _vehicle.data();
}

bool ThrusterDirectControlController::bindVehicle(Vehicle* vehicle)
{
    if (_vehicleBound) {
        return vehicle && _vehicle == vehicle;
    }
    if (!vehicle || vehicle->isOfflineEditingVehicle() || _state != Idle) {
        return false;
    }

    _vehicle = vehicle;
    _vehicleBound = true;
    _manualAdapter = false;
    connect(vehicle, &QObject::destroyed, this, [this]() {
        _vehicleUnavailable();
        emit vehicleChanged();
    });
    connect(vehicle->vehicleLinkManager(), &VehicleLinkManager::allLinksRemoved, this,
            &ThrusterDirectControlController::_vehicleUnavailable);
    connect(vehicle->vehicleLinkManager(), &VehicleLinkManager::communicationLostChanged, this, [this](bool lost) {
        if (lost) {
            _vehicleUnavailable();
        } else {
            _reissueChecksLeft = kReissueChecks;
            _checkOutputBeforeReissue();
        }
    });
    connect(vehicle, &Vehicle::armedChanged, this, [this](bool armed) {
        if (armed && busy()) {
            abort(functionSaveBusy() ? tr("保存准备期间飞控被解锁，未修改功能参数") : tr("直发期间飞控被解锁"));
        }
    });

    ParameterManager* manager = vehicle->parameterManager();
    connect(manager, &ParameterManager::_paramSetSuccess, this, [this](int componentId, const QString& name) {
        if (_parameterOperation == WritingParameter && _functionFact && componentId == _functionFact->componentId() &&
            name == _functionFact->name()) {
            _requestParameterRead();
        }
    });
    connect(manager, &ParameterManager::_paramSetFailure, this, [this](int componentId, const QString& name) {
        if (_parameterOperation == WritingParameter && _functionFact && componentId == _functionFact->componentId() &&
            name == _functionFact->name()) {
            _parameterFailed(tr("功能参数写入被拒绝或无回执"));
        }
    });
    connect(manager, &ParameterManager::_paramRequestReadSuccess, this,
            [this](int componentId, const QString& name, int) {
                if (_reissueCheckFact && componentId == _reissueCheckFact->componentId() &&
                    name == _reissueCheckFact->name()) {
                    // Disabled only if the vehicle said so during this read and the ground value agrees.
                    _outputCheckedForReissue(_reissueCheckValue.isValid() && _reissueCheckValue.toInt() == 0 &&
                                             _reissueCheckFact->rawValue().toInt() == 0);
                }
                if (_parameterOperation == ReadingParameter && _functionFact &&
                    componentId == _functionFact->componentId() && name == _functionFact->name()) {
                    const double value = _functionFact->rawValue().toDouble();
                    if (_state == SavingFunction) {
                        _functionSaveReadConfirmed(value);
                    } else if (_state == WaitingBackup) {
                        if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
                            value > std::numeric_limits<int>::max()) {
                            _rejectBeforeWrite(tr("飞控回读的原功能参数无效，未修改输出"));
                            return;
                        }
                        _stopParameterWait();
                        _originalFunction = static_cast<int>(value);
                        emit detailsChanged();
                        // Direct QML delivery persists the recovery journal before the
                        // first write. A close/abort triggered there must prevent it.
                        emit originalFunctionConfirmed(_sessionOutput, _originalFunction);
                        if (_state != WaitingBackup) {
                            return;
                        }
                        if (!_vehicleUsable(true)) {
                            _rejectBeforeWrite(tr("载具状态已变化，未修改输出"));
                            return;
                        }
                        _setState(WaitingDisable);
                        _startParameterWait(0, _parameterTimeoutMs);
                        _writeFunction(0);
                    } else if (std::isfinite(value) && qAbs(value - _expectedParameterValue) <= 0.0001) {
                        _parameterConfirmed(value);
                    } else {
                        _parameterFailed(tr("飞控回读的功能参数与目标值不一致"));
                    }
                }
            });
    connect(manager, &ParameterManager::_paramRequestReadFailure, this,
            [this](int componentId, const QString& name, int) {
                if (_reissueCheckFact && componentId == _reissueCheckFact->componentId() &&
                    name == _reissueCheckFact->name()) {
                    _outputCheckedForReissue(false);
                }
                if (_parameterOperation == ReadingParameter && _functionFact &&
                    componentId == _functionFact->componentId() && name == _functionFact->name()) {
                    _parameterFailed(tr("功能参数回读失败"));
                }
            });
    emit vehicleChanged();
    return true;
}

Fact* ThrusterDirectControlController::getParameterFact(int componentId, const QString& name, bool reportMissing)
{
    if (!_vehicle) {
        return nullptr;
    }
    ParameterManager* manager = _vehicle->parameterManager();
    if (!manager->parameterExists(componentId, name)) {
        if (reportMissing) {
            manager->getParameter(componentId, name);
        }
        return nullptr;
    }
    return manager->getParameter(componentId, name);
}

bool ThrusterDirectControlController::parameterExists(int componentId, const QString& name) const
{
    return _vehicle && _vehicle->parameterManager()->parameterExists(componentId, name);
}

bool ThrusterDirectControlController::_vehicleUsable(bool requireDisarmed) const
{
    if (_manualAdapter) {
        return true;
    }
    if (!_vehicle || _vehicle->isOfflineEditingVehicle() || (requireDisarmed && _vehicle->armed()) ||
        _vehicle->vehicleLinkManager()->communicationLost()) {
        return false;
    }
    const SharedLinkInterfacePtr link = _vehicle->vehicleLinkManager()->primaryLink().lock();
    return link && link->isConnected() && !link->linkConfiguration()->isHighLatency();
}

QString ThrusterDirectControlController::stateName() const
{
    switch (_state) {
        case WaitingBackup:
            return QStringLiteral("waitingBackup");
        case WaitingDisable:
            return QStringLiteral("waitingDisable");
        case DisableSettling:
            return QStringLiteral("disableSettling");
        case WaitingTestAck:
            return QStringLiteral("waitingTestAck");
        case Testing:
            return QStringLiteral("testing");
        case WaitingNeutralAck:
            return QStringLiteral("waitingNeutralAck");
        case WaitingRestore:
            return QStringLiteral("waitingRestore");
        case SavingFunction:
            return QStringLiteral("savingFunction");
        case RecoveryNeeded:
            return QStringLiteral("recoveryNeeded");
        case Idle:
        default:
            return QStringLiteral("idle");
    }
}

bool ThrusterDirectControlController::beginTest(int output, int originalFunction, int testPwm, int neutralPwm,
                                                int testDurationMs)
{
    if (_state != Idle || output < 1 || output > 16 || originalFunction < 0 || !_validPwm(testPwm) ||
        !_validPwm(neutralPwm) || testDurationMs < 1 || testDurationMs > 5000 || !_vehicleUsable(true)) {
        return false;
    }
    if (!_manualAdapter) {
        if (!_vehicle->parameterManager()->parametersReady() || _vehicle->parameterManager()->pendingWrites() ||
            _vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO)) {
            return false;
        }
        _functionFact = getParameterFact(-1, QStringLiteral("SERVO%1_FUNCTION").arg(output));
        if (!_functionFact) {
            return false;
        }
    }

    _sessionOutput = output;
    _pendingOutput = output;
    _output = -1;
    _originalFunction = _manualAdapter ? originalFunction : -1;
    _testPwm = testPwm;
    _neutralPwm = neutralPwm;
    _testDurationMs = testDurationMs;
    _stopRequested = false;
    _recovering = false;
    _neutralAcksNeeded = 1;
    _failureReason.clear();
    emit detailsChanged();

    if (_manualAdapter) {
        emit originalFunctionConfirmed(output, originalFunction);
        _setState(WaitingDisable);
        _startParameterWait(0, _parameterTimeoutMs);
        _writeFunction(0);
    } else {
        _setState(WaitingBackup);
        _startParameterWait(-1, _parameterTimeoutMs);
        _requestParameterRead();
    }
    return true;
}

bool ThrusterDirectControlController::beginRecovery(int output, int originalFunction, const QString& reason,
                                                    int neutralPwm)
{
    if (busy() || output < 1 || output > 16 || originalFunction < 0 || !_validPwm(neutralPwm) ||
        !_vehicleUsable(true)) {
        return false;
    }
    if (!_manualAdapter) {
        if (_vehicle->parameterManager()->pendingWrites() ||
            _vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO)) {
            return false;
        }
        _functionFact = getParameterFact(-1, QStringLiteral("SERVO%1_FUNCTION").arg(output));
        if (!_functionFact ||
            (_functionFact->rawValue().toInt() != 0 && _functionFact->rawValue().toInt() != originalFunction)) {
            return false;
        }
    }

    _stopTimers();
    _sessionOutput = output;
    _output = -1;
    _pendingOutput = output;
    _originalFunction = originalFunction;
    _neutralPwm = neutralPwm;
    // The output may still hold the test PWM of the run that left the record, before this one sends anything.
    _cancelReissue();
    _unconfirmedOutput = output;
    _unconfirmedNeutralPwm = neutralPwm;
    // Nothing is known about the PWM commands that led here, so one of them may still be answered late.
    _neutralAcksNeeded = 2;
    _stopRequested = true;
    _recovering = true;
    _failureReason = reason;
    emit detailsChanged();

    // The function may already have been restored while neutral was not. Make
    // this one output writable again, then confirm neutral before restoring it.
    _setState(WaitingDisable);
    _startParameterWait(0, _parameterTimeoutMs);
    _writeFunction(0);
    return true;
}

bool ThrusterDirectControlController::beginFunctionSave(int output, int expectedOriginal, int newFunction)
{
    const auto reject = [this](const QString& reason) {
        _failureReason = reason;
        emit detailsChanged();
        return false;
    };
    if (_state != Idle || output < 1 || output > 16) {
        return reject(tr("当前状态或输出编号无效，请先完成正在执行的测试、恢复或保存"));
    }
    if (!_functionSaveAvailable()) {
        return reject(tr("请保持载具在线且上锁，并等待参数写入和输出、解锁指令结束后再保存"));
    }
    Fact* fact = getParameterFact(-1, QStringLiteral("SERVO%1_FUNCTION").arg(output));
    if (!fact || fact->readOnly()) {
        return reject(tr("输出功能参数缺失或只读，无法保存"));
    }
    bool allowedFunction = false;
    for (const QVariant& enumValue : fact->enumValues()) {
        bool converted = false;
        if (enumValue.toLongLong(&converted) == newFunction && converted) {
            allowedFunction = true;
            break;
        }
    }
    if (!allowedFunction) {
        return reject(tr("所选功能不在飞控参数的可用选项中，无法保存"));
    }

    _functionFact = fact;
    _sessionOutput = output;
    _pendingOutput = output;
    _originalFunction = expectedOriginal;
    _newFunction = newFunction;
    _functionSaveWriteStarted = false;
    _failureReason.clear();
    emit detailsChanged();
    _setState(SavingFunction);
    if (_state == SavingFunction) {
        _startParameterWait(expectedOriginal, _parameterTimeoutMs);
        _requestParameterRead();
    }
    return true;
}

bool ThrusterDirectControlController::_functionSaveAvailable() const
{
    if (!_vehicle || MultiVehicleManager::instance()->activeVehicle() != _vehicle || !_vehicleUsable(true) ||
        !_vehicle->parameterManager()->parametersReady() || _vehicle->parameterManager()->pendingWrites()) {
        return false;
    }
    const int componentId = _vehicle->defaultComponentId();
    return !_vehicle->isMavCommandPending(componentId, MAV_CMD_DO_SET_SERVO) &&
           !_vehicle->isMavCommandPending(componentId, MAV_CMD_COMPONENT_ARM_DISARM) &&
           !_vehicle->isMavCommandPending(componentId, MAV_CMD_DO_MOTOR_TEST);
}

void ThrusterDirectControlController::_functionSaveReadConfirmed(double value)
{
    if (!_functionSaveAvailable() || !_functionFact || _functionFact->readOnly()) {
        _parameterFailed(tr("载具状态或功能参数已变化，无法确认保存结果"));
        return;
    }
    if (!std::isfinite(value) || qAbs(value - _expectedParameterValue) > 0.0001) {
        _parameterFailed(_functionSaveWriteStarted ? tr("飞控回读的功能参数与保存目标不一致")
                                                   : tr("飞控功能参数已变化，请重新选择并确认后再保存；未修改参数"));
        return;
    }
    if (_functionSaveWriteStarted || _newFunction == _originalFunction) {
        _finishFunctionSave(true, tr("SERVO%1_FUNCTION=%2 已由飞控回读确认").arg(_sessionOutput).arg(_newFunction));
        return;
    }

    _functionSaveWriteStarted = true;
    _startParameterWait(_newFunction, _parameterTimeoutMs);
    _writeFunction(_newFunction);
}

void ThrusterDirectControlController::_finishFunctionSave(bool success, const QString& message)
{
    const int output = _sessionOutput;
    _stopTimers();
    _clearSession();
    if (!success) {
        _failureReason = message;
        emit detailsChanged();
    }
    _setState(Idle);
    emit functionSaveFinished(output, success, message);
}

void ThrusterDirectControlController::_writeFunction(int value)
{
    emit functionWriteRequested(value);
    if (_manualAdapter) {
        return;
    }
    if (!_vehicleUsable(true) || !_functionFact) {
        _parameterFailed(functionSaveBusy() ? tr("载具连接或功能参数不可用，保存未确认")
                                            : tr("载具连接或功能参数不可用，请保持上锁后恢复"));
        return;
    }
    if (_functionFact->rawValue().toInt() == value) {
        _requestParameterRead();
        return;
    }
    _parameterOperation = WritingParameter;
    _functionFact->setRawValue(value);
}

void ThrusterDirectControlController::_requestParameterRead()
{
    if (!_vehicle || !_functionFact || !_vehicleUsable(true)) {
        _parameterFailed(functionSaveBusy() ? tr("载具连接或功能参数不可用，保存未确认")
                                            : tr("载具连接或功能参数不可用，请保持上锁后恢复"));
        return;
    }
    _parameterOperation = ReadingParameter;
    _vehicle->parameterManager()->refreshParameter(_functionFact->componentId(), _functionFact->name());
}

void ThrusterDirectControlController::_parameterConfirmed(double rawValue)
{
    if ((_state != WaitingDisable && _state != WaitingRestore) || !std::isfinite(rawValue) ||
        qAbs(rawValue - static_cast<double>(_expectedParameterValue)) > 0.0001) {
        return;
    }
    _stopParameterWait();
    if (_state == WaitingDisable) {
        if (_stopRequested && !_recovering) {
            _requestRestore(_failureReason);
        } else {
            _setState(DisableSettling);
            _settleTimer.start(_settleMs);
        }
    } else {
        _completeRecovery();
    }
}

void ThrusterDirectControlController::_parameterFailed(const QString& reason)
{
    // A failed write leaves a locally optimistic Fact value. Do not perform a
    // same-value "restore" and mistake that cache value for a vehicle response.
    if (_state == SavingFunction) {
        _finishFunctionSave(false, _functionSaveWriteStarted
                                       ? tr("%1；修改结果未确认，请回读检查实际参数，不会自动回滚").arg(reason)
                                       : tr("%1；未修改功能参数").arg(reason));
    } else if (_state == WaitingBackup) {
        _rejectBeforeWrite(reason);
    } else {
        _requireRecovery(reason);
    }
}

void ThrusterDirectControlController::_rejectBeforeWrite(const QString& reason)
{
    _stopTimers();
    _clearSession();
    _failureReason = reason;
    emit detailsChanged();
    _setState(Idle);
    emit testRejected(reason);
}

void ThrusterDirectControlController::_sendPwm(int pwm)
{
    const quint64 token = ++_commandToken;
    _commandPending = true;
    if (_state == WaitingTestAck) {
        _testDurationTimer.start(_testDurationMs);
    }
    _commandTimer.start(_commandTimeoutMs);
    emit servoPwmRequested(_output, pwm);
    if (_manualAdapter) {
        return;
    }
    if (!_vehicleUsable(false)) {
        _requireRecovery(tr("载具连接已丢失，无法确认 PWM 回中"));
        return;
    }

    auto* context = new ServoCommandContext(_vehicle.data(), this, token);
    Vehicle::MavCmdAckHandlerInfo_t handler{};
    handler.resultHandler = [](void* data, int, const mavlink_command_ack_t& ack,
                               Vehicle::MavCmdResultFailureCode_t failureCode) {
        auto* context = static_cast<ServoCommandContext*>(data);
        QPointer<ServoCommandContext> guardedContext(context);
        if (context->controller) {
            context->controller->_handleCommandResult(ack.result, failureCode, context->token);
        }
        if (guardedContext) {
            guardedContext->deleteLater();
        }
    };
    handler.resultHandlerData = context;
    _vehicle->sendMavCommandWithHandler(&handler, _vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO, _output, pwm);
}

void ThrusterDirectControlController::_handleCommandResult(int ackResult, int failureCode, quint64 token)
{
    if (token != _commandToken || ackResult == MAV_RESULT_IN_PROGRESS) {
        return;
    }
    _commandPending = false;
    _commandTimer.stop();
    if (_state == WaitingTestAck) {
        if (failureCode == Vehicle::MavCmdResultFailureDuplicateCommand) {
            _requireRecovery(tr("PWM 指令与另一条未完成指令冲突，未确认回中"));
        } else if (failureCode == Vehicle::MavCmdResultFailureNoResponseToCommand) {
            _failureReason = tr("测试 PWM 指令无回执");
            _neutralAcksNeeded = 2;
            emit detailsChanged();
            _beginNeutral();
        } else if (ackResult == MAV_RESULT_ACCEPTED && failureCode == Vehicle::MavCmdResultCommandResultOnly) {
            if (_stopRequested) {
                _beginNeutral();
            } else {
                _setState(Testing);
            }
        } else if (failureCode == Vehicle::MavCmdResultCommandResultOnly) {
            _requestRestore(tr("飞控拒绝测试 PWM 指令"));
        } else {
            _requireRecovery(tr("PWM 指令结果不明确，未确认回中"));
        }
        return;
    }
    if (_state == WaitingNeutralAck) {
        if (ackResult == MAV_RESULT_ACCEPTED && failureCode == Vehicle::MavCmdResultCommandResultOnly) {
            if (_neutralAcksNeeded > 1) {
                // This reply may belong to an earlier unanswered PWM command, so neutral is requested again.
                // That request counts against the same budget as every other neutral of the session.
                --_neutralAcksNeeded;
                _retryNeutralOrRequireRecovery(tr("PWM 回中只得到一次回执，无法排除它属于更早的指令；请恢复异常通道"));
                return;
            }
            // What is left is the parameter, which the recovery record covers. A parameter failure after
            // this point must not lead to another PWM on an output that may have its function back.
            _forgetUnconfirmedNeutral();
            _requestRestore(QString());
        } else if (failureCode == Vehicle::MavCmdResultFailureDuplicateCommand) {
            // The queue rejected the command without sending it; repeating at once would be rejected again.
            _requireRecovery(tr("PWM 回中指令与另一条未完成指令冲突，请恢复异常通道"));
        } else {
            _retryNeutralOrRequireRecovery(tr("PWM 回中未确认，请保持上锁后恢复异常通道"));
        }
    }
}

void ThrusterDirectControlController::abort(const QString& reason)
{
    if (!busy()) {
        return;
    }
    if (_state == SavingFunction) {
        if (!_functionSaveWriteStarted) {
            _finishFunctionSave(false, reason.isEmpty() ? tr("保存准备已取消，未修改功能参数") : reason);
        }
        // A permanent PARAM_SET must resolve; abort must never restore its old value.
        return;
    }
    if (_state == WaitingBackup) {
        _rejectBeforeWrite(reason);
        return;
    }
    _stopRequested = true;
    if (!reason.isEmpty()) {
        _failureReason = reason;
        emit detailsChanged();
    }
    if (_state == Testing) {
        _beginNeutral();
    } else if (_state == DisableSettling && !_recovering) {
        _requestRestore(reason);
    }
    // A pending PARAM_SET or SET_SERVO must resolve before sending another
    // command of the same kind. The completion path honors _stopRequested.
}

void ThrusterDirectControlController::finishTest()
{
    if (_state == Testing) {
        _beginNeutral();
    } else if (_state == WaitingTestAck) {
        _stopRequested = true;
    }
}

void ThrusterDirectControlController::_requestRestore(const QString& reason)
{
    if (_state == SavingFunction) {
        abort(reason);
        return;
    }
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
    _writeFunction(_originalFunction);
}

QString ThrusterDirectControlController::pendingRecoveryNotice()
{
    // The record is written by the Settings object of ThrusterMappingTool.qml under these keys.
    QSettings journal;
    journal.beginGroup(QStringLiteral("DeepSharkServoOutputMapping"));
    if (!journal.value(QStringLiteral("recoveryPending"), false).toBool()) {
        return QString();
    }
    return tr("检测到未完成的输出测试记录：SERVO%1，原功能 %2，记录于 %3。"
              "如果现在没有输出测试在进行，这一路可能仍处于禁用状态。请先核对记录是否属于当前连接的机器人，"
              "再在参数页的工具菜单打开“输出测试与接线记录”，用“恢复异常通道”恢复；恢复前不要下水。")
        .arg(journal.value(QStringLiteral("recoveryOutput"), -1).toInt())
        .arg(journal.value(QStringLiteral("recoveryFunction"), -1).toInt())
        .arg(journal.value(QStringLiteral("recoveryTimestamp")).toString());
}

#ifdef QGC_UNITTEST_BUILD
void ThrusterDirectControlController::reportParameterUnavailable(const QString& reason)
{
    if (busy()) {
        _parameterFailed(reason);
    }
}
#endif

void ThrusterDirectControlController::_vehicleUnavailable()
{
    if (_state == SavingFunction) {
        _finishFunctionSave(false, _functionSaveWriteStarted
                                       ? tr("载具连接已丢失，功能修改结果未确认；请重连后检查实际参数，不会自动回滚")
                                       : tr("载具连接已丢失，未修改功能参数"));
    } else if (_state == WaitingBackup) {
        _rejectBeforeWrite(tr("载具连接已丢失，未修改输出"));
    } else if (busy() || _state == RecoveryNeeded) {
        const bool outputWasLive = _output >= 1;
        // The vehicle cannot be asked any more. What the ground side knows must at least not contradict
        // "this output is still the one this session disabled".
        const bool knownDisabled = _manualAdapter ||
                                   (_functionFact && _functionFact->rawValue().toInt() == 0 && _vehicle &&
                                    !_vehicle->parameterManager()->pendingWrites());
        _cancelReissue();
        _requireRecovery(tr("载具连接已丢失，恢复记录保留；请重新连接对应载具后恢复"));
        if (outputWasLive && knownDisabled) {
            // This session disabled the output moments ago, and a missing heartbeat does not prove that
            // nothing gets through in the other direction. A session that was already waiting for manual
            // recovery sends nothing here: its output may have been given another function since.
            _sendUnconfirmedNeutral();
        } else if (outputWasLive && _unconfirmedOutput >= 1) {
            emit neutralReissueSkipped(_unconfirmedOutput);
        }
    }
    _cancelReissue();
    _functionFact.clear();
}

bool ThrusterDirectControlController::reset()
{
    if (busy() || _commandPending) {
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
    _manualAdapter = !_vehicleBound;
    _settleMs = qMax(1, settleMs);
    _commandTimeoutMs = qMax(1, commandTimeoutMs);
    _parameterTimeoutMs = qMax(1, parameterTimeoutMs);
    _restoreParameterTimeoutMs = _parameterTimeoutMs;
}

void ThrusterDirectControlController::observeParameterState(double rawValue, bool pendingWrites)
{
    if (_manualAdapter && !pendingWrites) {
        _parameterConfirmed(rawValue);
    }
}

void ThrusterDirectControlController::handleCommandResult(int ackResult, int failureCode)
{
    if (_manualAdapter) {
        _handleCommandResult(ackResult, failureCode, _commandToken);
    }
}
#endif

void ThrusterDirectControlController::_settleFinished()
{
    if (_state != DisableSettling || _pendingOutput < 1 || !_vehicleUsable(true)) {
        _requireRecovery(tr("载具连接或直测状态发生变化"));
        return;
    }
    _output = _pendingOutput;
    _pendingOutput = -1;
    emit detailsChanged();
    if (_recovering) {
        _beginNeutral();
    } else {
        _setState(WaitingTestAck);
        _sendPwm(_testPwm);
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
        _neutralAcksNeeded = 2;
        _stopRequested = true;
        emit detailsChanged();
        if (_manualAdapter || !_vehicle ||
            !_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO)) {
            _commandPending = false;
            _beginNeutral();
        }
    } else if (_state == WaitingNeutralAck) {
        const QString reason = tr("PWM 回中指令无回执，请恢复异常通道");
        if (!_manualAdapter && _vehicle &&
            _vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO)) {
            // The queue would reject a second command of the same kind.
            _requireRecovery(reason);
        } else {
            _commandPending = false;
            _retryNeutralOrRequireRecovery(reason);
        }
    }
}

void ThrusterDirectControlController::_parameterTimedOut()
{
    if (_state == SavingFunction) {
        _finishFunctionSave(false, _functionSaveWriteStarted
                                       ? tr("功能保存或回读超时，修改结果未确认；请检查实际参数，不会自动回滚")
                                       : tr("保存前功能回读超时，未修改参数"));
    } else if (_state == WaitingBackup) {
        _rejectBeforeWrite(tr("原功能参数回读超时，未修改输出"));
    } else {
        _requireRecovery(_state == WaitingDisable ? tr("等待 Disabled 确认超时") : tr("等待原功能参数回读确认超时"));
    }
}

void ThrusterDirectControlController::_setState(State state)
{
    if (_state != state) {
        _state = state;
        emit stateChanged();
    }
}

void ThrusterDirectControlController::_startParameterWait(int expectedValue, int timeoutMs)
{
    _expectedParameterValue = expectedValue;
    _parameterTimeoutTimer.start(timeoutMs);
}

void ThrusterDirectControlController::_stopParameterWait()
{
    _parameterOperation = NoParameterOperation;
    _parameterTimeoutTimer.stop();
}

void ThrusterDirectControlController::_beginNeutral()
{
    if (_output < 1) {
        _requireRecovery(tr("测试通道状态无效，未确认回中"));
        return;
    }
    _testDurationTimer.stop();
    _commandTimer.stop();
    _neutralAttemptsLeft = kNeutralSendAttempts - 1;
    _setState(WaitingNeutralAck);
    _sendPwm(_neutralPwm);
}

void ThrusterDirectControlController::_retryNeutralOrRequireRecovery(const QString& reason)
{
    if (_neutralAttemptsLeft < 1) {
        _requireRecovery(reason);
        return;
    }
    --_neutralAttemptsLeft;
    _commandTimer.stop();
    _sendPwm(_neutralPwm);
}

void ThrusterDirectControlController::_cancelReissue()
{
    // An answer to an earlier question, or a wait started for it, must not release a PWM later on.
    _reissueTimer.stop();
    _reissueChecksLeft = 0;
    QObject::disconnect(_reissueCheckUpdate);
    _reissueCheckFact.clear();
    _reissueCheckValue.clear();
}

void ThrusterDirectControlController::_forgetUnconfirmedNeutral()
{
    _cancelReissue();
    _unconfirmedOutput = -1;
}

bool ThrusterDirectControlController::_servoCommandOrParameterWritePending() const
{
    return _vehicle && (_vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO) ||
                        _vehicle->parameterManager()->pendingWrites());
}

void ThrusterDirectControlController::_checkOutputBeforeReissue()
{
    // The link of the same vehicle is back. Whether the output is still the one this session disabled is
    // not known from here: its function may have been changed meanwhile, and a neutral PWM would then be
    // written over a working configuration. Ask the vehicle, and send only right after it answered.
    if (_state != RecoveryNeeded || _unconfirmedOutput < 1 || _manualAdapter) {
        return;
    }
    const int output = _unconfirmedOutput;
    const int checksLeft = _reissueChecksLeft;
    _cancelReissue();
    if (!_vehicleUsable(false)) {
        emit neutralReissueSkipped(output);
        return;
    }
    if (_servoCommandOrParameterWritePending()) {
        // The queue would reject a second servo command, and a pending write may be changing the function.
        // Wait for both, then ask again from the start: an earlier answer is not kept across the wait.
        if (checksLeft > 0) {
            _reissueChecksLeft = checksLeft - 1;
            _reissueTimer.start(kReissueCheckIntervalMs);
        } else {
            emit neutralReissueSkipped(output);
        }
        return;
    }
    Fact* const fact = getParameterFact(-1, QStringLiteral("SERVO%1_FUNCTION").arg(output));
    if (!fact) {
        emit neutralReissueSkipped(output);
        return;
    }
    _reissueChecksLeft = checksLeft;
    _reissueCheckFact = fact;
    _reissueCheckUpdate = connect(fact, &Fact::vehicleUpdated, this,
                                  [this](const QVariant& value) { _reissueCheckValue = value; });
    _vehicle->parameterManager()->refreshParameter(fact->componentId(), fact->name());
}

void ThrusterDirectControlController::_outputCheckedForReissue(bool stillDisabled)
{
    const int checksLeft = _reissueChecksLeft;
    _cancelReissue();
    if (_state != RecoveryNeeded || _unconfirmedOutput < 1) {
        return;
    }
    if (!stillDisabled || !_vehicleUsable(false)) {
        emit neutralReissueSkipped(_unconfirmedOutput);
        return;
    }
    if (_servoCommandOrParameterWritePending()) {
        // Something started while the vehicle was answering. This answer is not used later.
        if (checksLeft > 0) {
            _reissueChecksLeft = checksLeft - 1;
            _reissueTimer.start(kReissueCheckIntervalMs);
        } else {
            emit neutralReissueSkipped(_unconfirmedOutput);
        }
        return;
    }
    _sendUnconfirmedNeutral();
}

void ThrusterDirectControlController::_sendUnconfirmedNeutral()
{
    // Best effort for an output that may still hold a test PWM. The session stays in manual recovery,
    // which confirms neutral itself before it writes the original function back. The caller has decided
    // that the output may be written; nothing is checked or waited for here.
    if (_state != RecoveryNeeded || _unconfirmedOutput < 1) {
        return;
    }
    if (!_manualAdapter) {
        if (!_vehicle || !_vehicle->vehicleLinkManager()->primaryLink().lock() ||
            _vehicle->isMavCommandPending(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO)) {
            // Without a link, or with a servo command already pending, the queue would not send this one.
            return;
        }
        _vehicle->sendMavCommand(_vehicle->defaultComponentId(), MAV_CMD_DO_SET_SERVO, false /* showError */,
                                 _unconfirmedOutput, _unconfirmedNeutralPwm);
    }
    emit servoPwmRequested(_unconfirmedOutput, _unconfirmedNeutralPwm);
    emit neutralReissued(_unconfirmedOutput);
}

void ThrusterDirectControlController::_requireRecovery(const QString& reason)
{
    if (_output >= 1) {
        _unconfirmedOutput = _output;
        _unconfirmedNeutralPwm = _neutralPwm;
    }
    _stopTimers();
    ++_commandToken;
    _commandPending = false;
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
    ++_commandToken;
    _commandPending = false;
    _functionFact.clear();
    _sessionOutput = -1;
    _output = -1;
    _pendingOutput = -1;
    _originalFunction = -1;
    _newFunction = -1;
    _testPwm = 1500;
    _neutralPwm = 1500;
    _testDurationMs = 1000;
    _expectedParameterValue = -1;
    _stopRequested = false;
    _recovering = false;
    _functionSaveWriteStarted = false;
    _neutralAttemptsLeft = 0;
    _neutralAcksNeeded = 1;
    _forgetUnconfirmedNeutral();
    _failureReason.clear();
    emit detailsChanged();
}

bool ThrusterDirectControlController::_validPwm(int pwm)
{
    return pwm >= 800 && pwm <= 2200;
}

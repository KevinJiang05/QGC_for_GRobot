/****************************************************************************
 *
 * DeepShark SERVO/output mapping helper.
 *
 ****************************************************************************/

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore

import QGroundControl
import QGroundControl.Controls
import DeepShark

QGCPopupDialog {
    id:             root
    title:          qsTr("输出测试与接线记录")
    buttons:        Dialog.Close

    readonly property var currentActiveVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var activeVehicle:         directControl.vehicle
    readonly property bool vehicleContextMatches: activeVehicle !== null
                                                          && !activeVehicle.isOfflineEditingVehicle
                                                          && currentActiveVehicle === activeVehicle
    readonly property string sessionVehicleText: activeVehicle
                                                          ? qsTr("载具 %1 / UID %2")
                                                                .arg(activeVehicle.id)
                                                                .arg(Number(activeVehicle.vehicleUID) > 0 ? activeVehicle.vehicleUIDStr : qsTr("未上报"))
                                                          : qsTr("无载具")
    property bool   testEnabled:        false
    property int    runningOutput:      -1
    property int    runningMotorTarget: -1
    property int    motorTestPulsesRemaining: 0
    // A single selection owns all test paths: Motor Test, direct PWM, servo jog.
    property int    testMode:           0
    readonly property bool directServoMode: testMode === 1
    readonly property bool servoJogMode: testMode === 2
    readonly property bool vehicleReady: vehicleContextMatches
                                                && activeVehicle.parameterManager.parametersReady
                                                && !activeVehicle.vehicleLinkManager.communicationLost
    // "Disable safety restrictions" in the application settings. With it on, the tool keeps to what cannot
    // be done at all and leaves the rest to the operator.
    readonly property bool restrictionsOff: vehicleContextMatches && activeVehicle.setupSafetyRestrictionsDisabled
    readonly property bool vehicleArmingUnconfirmed: activeVehicle !== null && activeVehicle.armingStateUnconfirmed
    readonly property bool testPathAvailable: vehicleReady
                                                && !vehicleArmingUnconfirmed
                                                && !directControl.busy && !mappingSettings.recoveryPending
                                                && (restrictionsOff
                                                    || (!disarmRequested && !disarmUnconfirmed && !armingRequested))
                                                && !armCommandPending && runningOutput === -1
                                                && !activeVehicle.parameterManager.pendingWrites
    readonly property bool canPrepareMotorTest: testEnabled && testMode === 0 && testPathAvailable && cooldownRemaining === 0
    readonly property bool canStartMotorTest: canPrepareMotorTest && activeVehicle.armed
    readonly property bool canStartDirectTest: testEnabled && testMode !== 0 && testPathAvailable && !activeVehicle.armed
    readonly property bool canRecordMappings: vehicleContextMatches && mappingStorageValid
    readonly property bool canConfigureServoFunctions: testPathAvailable && directControl.stateName === "idle"
                                                           && !testEnabled && !testSessionOwned && !activeVehicle.armed
    property var functionSaveConfirmation: null
    property string functionSaveStatusText: ""
    readonly property string currentVehicleUid: activeVehicle && Number(activeVehicle.vehicleUID) > 0
                                                        ? activeVehicle.vehicleUIDStr : ""
    readonly property string mappingScopeText: recordVehicleUid.length > 0
                                                        ? qsTr("接线记录按载具 UID %1 单独保存").arg(recordVehicleUid)
                                                        : currentVehicleUid.length > 0
                                                          ? qsTr("本次接线记录保留在当前会话；该载具已有记录，请先导出再重新打开")
                                                          : qsTr("载具未上报 UID；接线记录仅保存在当前会话")
    property string recordVehicleUid: ""
    property int recordVehicleId: -1
    property var recordPorts: []
    property var recordPositions: []
    property bool recordsDirty: false
    property bool mappingStorageValid: true
    property bool legacyMappingsAvailable: false
    property bool legacyImportConfirmation: false
    property bool clearMappingsConfirmation: false
    property bool testSessionOwned: false
    property string directServoOperation: "thruster"
    readonly property int directServoOutput: directControl.output
    readonly property int directServoPendingOutput: directControl.pendingOutput
    readonly property int directServoOriginalFunction: directControl.originalFunction
    property int    directServoTestPwm: 1500
    property int    directServoTestSeconds: 1
    property int    directServoNeutralPwm: 1500
    property var    directServoFact:    null
    readonly property string directServoState: directControl.stateName
    readonly property string directServoFailureReason: directControl.failureReason
    property bool   recoveryDiscardConfirmation: false
    // A recovery of an output that keeps taking direct PWM commands with its original function finishes
    // only on the operator's word; see ThrusterDirectControlController::recoveryNeedsOperatorConfirmation().
    property bool   recoveryOperatorConfirmation: false
    property bool   recoveryForceConfirmation: false
    property bool   recoverySessionActive: false
    // What the operator has to check about the other outputs after a session. It stays until the next
    // session ends without it, whatever the status line shows in between.
    property string outputMappingNotice: ""
    property int    outputMappingNoticeOutput: -1
    property bool   outputMappingNoticeAfterRecovery: false
    // A record without a serial number cannot be matched to a vehicle. The operator may vouch for it,
    // for this session only, unless that is switched off in the application settings.
    property bool   recoveryManualConfirmed: false
    property bool   recoveryManualConfirmation: false
    readonly property bool recoveryManualConfirmAvailable: mappingSettings.recoveryPending && vehicleReady
                                                && recoveryVehicleObject === null
                                                && mappingSettings.recoveryVehicleUid.length === 0
                                                && mappingSettings.recoveryVehicleId === activeVehicle.id
                                                && !recoveryManualConfirmed && !directControl.busy
                                                && manualRecoveryAllowed
    readonly property bool manualRecoveryAllowed: QGroundControl.settingsManager.appSettings.allowManualOutputRecovery.rawValue
    // The record is tied to this vehicle only by the operator's word, so the recovery needs the setting.
    readonly property bool recoveryRestsOnManualConfirmation: mappingSettings.recoveryPending
                                                && recoveryVehicleObject === null
                                                && mappingSettings.recoveryVehicleUid.length === 0
    readonly property bool recoveryPermitted: !recoveryRestsOnManualConfirmation || manualRecoveryAllowed

    onManualRecoveryAllowedChanged: {
        if (!manualRecoveryAllowed) {
            // A half-given confirmation is dropped. A completed one stays as the operator's statement about
            // the vehicle, which keeps the record from being discarded, but it no longer permits a recovery.
            manualRecoveryConfirmTimer.stop()
            recoveryManualConfirmation = false
        }
    }

    function revokeManualRecoveryConfirmation() {
        manualRecoveryConfirmTimer.stop()
        recoveryManualConfirmation = false
        recoveryManualConfirmed = false
    }
    property var    recoveryVehicleObject: null
    property bool   disarmRequested:     false
    property bool   closeAfterSafeShutdown: false
    property int    cooldownRemaining:  0
    property bool   armingRequested:    false
    property bool   armCommandPending:  false
    property bool   stopAfterArming:    false
    property bool   disarmAckConfirmed: false
    property bool   disarmUnconfirmed:  false
    property string testStatusText:     ""
    property string directServoStatusText: ""
    readonly property int motorTestCommand: 209
    readonly property int armDisarmCommand: 400
    readonly property int autopilotComponentId: 1
    property var    appSettings:        QGroundControl.settingsManager.appSettings
    property var    positionNames: [
        qsTr("未记录"),
        qsTr("左前水平"),
        qsTr("右前水平"),
        qsTr("左后水平"),
        qsTr("右后水平"),
        qsTr("左前垂直"),
        qsTr("右前垂直"),
        qsTr("左后垂直"),
        qsTr("右后垂直"),
        qsTr("非推进器/未接")
    ]

    signal mappingsUpdated()

    ThrusterMappingExportController {
        id: exportController
    }

    ThrusterDirectControlController {
        id: directControl
        objectName: "thrusterDirectControl"
        operatorOverride: root.restrictionsOff

        onStateChanged: {
            switch (directControl.stateName) {
            case "waitingBackup":
                root.directServoStatusText = qsTr("正在从飞控回读 SERVO%1 原功能；确认后才会开始测试。")
                        .arg(directControl.pendingOutput)
                break
            case "disableSettling":
                root.directServoStatusText = qsTr("%1 已确认 Disabled，准备发送 PWM。")
                        .arg(root.directServoFact ? root.directServoFact.name : root.servoParamName(mappingSettings.recoveryOutput))
                break
            case "waitingTestAck":
                root.directServoStatusText = qsTr("已发送 SERVO%1=%2 PWM，等待飞控确认。")
                        .arg(directControl.output)
                        .arg(root.directServoTestPwm)
                break
            case "testing":
                root.directServoStatusText = qsTr("正在直接输出 SERVO%1 = %2 PWM。")
                        .arg(directControl.output)
                        .arg(root.directServoTestPwm)
                break
            case "waitingNeutralAck":
                root.directServoStatusText = qsTr("正在将 SERVO%1 回中到 %2 PWM，等待飞控确认。")
                        .arg(directControl.output)
                        .arg(root.directServoNeutralPwm)
                break
            case "confirmingNeutral":
                root.directServoStatusText = qsTr("收到接受回执，正在核对飞控上报的 SERVO%1 输出是否回到 %2 PWM；尚未确认回中。")
                        .arg(directControl.output)
                        .arg(root.directServoNeutralPwm)
                break
            case "waitingRestore":
                root.directServoStatusText = root.directServoFact
                        ? qsTr("正在恢复 %1=%2。").arg(root.directServoFact.name).arg(directControl.originalFunction)
                        : qsTr("正在恢复原功能参数。")
                break
            }
        }

        onRecoveryCompleted: (output, failureReason) => {
            var afterRecovery = root.recoverySessionActive
            root.recoverySessionActive = false
            root.clearRecoveryJournal()
            root.directServoFact = null
            root.directServoNeutralPwm = 1500
            root.checkOutputMapping(output, afterRecovery)
            root.directServoStatusText = failureReason.length > 0
                    ? qsTr("SERVO%1 的原功能参数已恢复（飞控上报该输出在中位后写回）；本次%2提前结束：%3。").arg(output).arg(root.directServoOperationText()).arg(failureReason)
                    : qsTr("飞控上报 SERVO%1 的输出已回到中位，原功能参数已恢复并回读确认。").arg(output)
            root.requestSafeDisarm(root.closeAfterSafeShutdown, qsTr("%1已结束").arg(root.directServoOperationText()))
        }

        onOutputFunctionsRefreshed: (complete) => {
            if (root.outputMappingNoticeOutput < 1) {
                return
            }
            if (!complete) {
                root.outputMappingNotice = qsTr("SERVO%1 的回中和功能参数已恢复，但没能把其他输出的功能全部重新读回来，所以没有核对它们。请手动刷新参数，核对全部输出功能（特别是有没有另一路也是同一个电机功能）之后再下水。").arg(root.outputMappingNoticeOutput)
            } else if (!root.directServoBusy()) {
                root.updateOutputMappingNotice(root.outputMappingNoticeOutput, root.outputMappingNoticeAfterRecovery, true)
            }
        }

        onRecoveryRequired: (reason) => {
            root.recoverySessionActive = false
            root.closeAfterSafeShutdown = false
            root.directServoStatusText = qsTr("恢复尚未确认：%1。请保持飞控上锁并使用“恢复异常通道”。").arg(reason)
        }

        onNeutralReissueSkipped: (output) => {
            root.directServoStatusText = qsTr("链路已恢复，但无法确认 SERVO%1 仍处于禁用状态，没有自动发送回中。请保持飞控上锁并使用“恢复异常通道”。").arg(output)
        }

        onNeutralReissued: (output) => {
            root.directServoStatusText = qsTr("已再次向 SERVO%1 发送回中指令，但没有确认。请保持飞控上锁并使用“恢复异常通道”。").arg(output)
        }

        onOriginalFunctionConfirmed: (output, originalFunction) => {
            // The backup is in the store before the controller writes Disabled.
            root.saveRecoveryJournal(output, originalFunction)
        }

        onTestRejected: (reason) => {
            root.clearRecoveryJournal()
            root.directServoFact = null
            root.directServoStatusText = qsTr("测试未开始：%1；未修改飞控参数。").arg(reason)
            root.testSessionOwned = false
            if (root.closeAfterSafeShutdown) {
                root.requestSafeDisarm(true, qsTr("测试准备已取消"))
            }
        }

        onFunctionSaveFinished: (output, success, message) => {
            root.functionSaveStatusText = success
                    ? message
                    : qsTr("SERVO%1 保存未确认：%2。请检查连接及飞控实际参数后重试。").arg(output).arg(message)
            if (root.closeAfterSafeShutdown) {
                root.beginSafeShutdown(true, qsTr("参数保存已结束"))
            }
        }

        onVehicleChanged: {
            if (root.activeVehicle === null && root.recordVehicleId >= 0) {
                root.directServoFact = null
                root.armCommandPending = false
                root.armingRequested = false
                root.disarmRequested = false
                root.stopAfterArming = false
                root.testSessionOwned = false
                root.disarmUnconfirmed = false
                root.closeAfterSafeShutdown = false
                root.beginSafeShutdown(false, qsTr("载具连接已断开"))
                root.testStatusText = qsTr("连接已断开；请重新连接并检查实际输出和上锁状态。")
            }
        }
    }

    Settings {
        id:         mappingSettings
        objectName: "thrusterMappingSettings"
        category:   "DeepSharkServoOutputMapping"

        property int    testPercent:    5
        property int    testSeconds:    1
        property bool   useServoFunctionForMotorTest: false
        property int    directServoPwm: 1550
        property int    directServoSeconds: 1
        property int    servoJogPwm:    1200
        property int    servoJogSeconds: 1
        property string vehicleMappings: "{}"

        // Recovery is scoped to one vehicle/output. Obsolete whole-vehicle
        // backup settings are intentionally not loaded or restored.
        property bool   recoveryPending: false
        property int    recoveryVehicleId: -1
        property string recoveryVehicleUid: ""
        property int    recoveryOutput: -1
        property int    recoveryFunction: -1
        property string recoveryTimestamp: ""
        property int    recoveryNeutralPwm: 1500
        property string recoveryOperation: "thruster"

        property string port1Name:      ""
        property string port2Name:      ""
        property string port3Name:      ""
        property string port4Name:      ""
        property string port5Name:      ""
        property string port6Name:      ""
        property string port7Name:      ""
        property string port8Name:      ""
        property string port9Name:      ""
        property string port10Name:     ""
        property string port11Name:     ""
        property string port12Name:     ""
        property string port13Name:     ""
        property string port14Name:     ""
        property string port15Name:     ""
        property string port16Name:     ""

        property int    servo1Position: 0
        property int    servo2Position: 0
        property int    servo3Position: 0
        property int    servo4Position: 0
        property int    servo5Position: 0
        property int    servo6Position: 0
        property int    servo7Position: 0
        property int    servo8Position: 0
        property int    servo9Position: 0
        property int    servo10Position: 0
        property int    servo11Position: 0
        property int    servo12Position: 0
        property int    servo13Position: 0
        property int    servo14Position: 0
        property int    servo15Position: 0
        property int    servo16Position: 0
    }

    Component.onCompleted: {
        if (mappingSettings.directServoPwm < 1000 || mappingSettings.directServoPwm > 2200) {
            mappingSettings.directServoPwm = 1550
            root.testStatusText = qsTr("旧 PWM 设置超出 1000–2200 范围，已重置为 1550；测试前请确认目标值。")
        }
        directControl.bindVehicle(root.currentActiveVehicle)
        root.recordVehicleId = root.activeVehicle ? root.activeVehicle.id : -1
        root.loadMappings()
    }

    onRejected: {
        if (root.directServoBusy()
                || root.disarmRequested
                || root.runningOutput !== -1
                || root.testEnabled || root.testSessionOwned || root.armingRequested || root.armCommandPending
                || root.vehicleArmingUnconfirmed) {
            preventClose = true
            root.beginSafeShutdown(true, qsTr("用户请求关闭输出测试工具"))
        }
    }

    onClosed: {
        testEnabled = false
        motorTestPulseTimer.stop()
        safeDisarmTimeout.stop()
        testCooldown.stop()
    }

    onCurrentActiveVehicleChanged: {
        if (root.vehicleContextMatches) {
            return
        }

        root.revokeManualRecoveryConfirmation()
        if (root.testSessionOwned || root.directServoBusy() || root.runningOutput !== -1) {
            root.beginSafeShutdown(false, qsTr("活动载具已切换"))
        } else {
            root.testEnabled = false
        }
        if (!root.directServoBusy() && root.activeVehicle) {
            root.directServoStatusText = qsTr("活动载具与工具锁定载具不一致，测试功能已停用。")
        }
    }

    function servoParamName(outputNumber) {
        return "SERVO" + outputNumber + "_FUNCTION"
    }

    function servoTrimParamName(outputNumber) {
        return "SERVO" + outputNumber + "_TRIM"
    }

    function servoLimitParamName(outputNumber, suffix) {
        return "SERVO" + outputNumber + "_" + suffix
    }

    function servoFunctionFact(outputNumber) {
        if (!root.activeVehicle || !root.activeVehicle.parameterManager.parametersReady
                || !directControl.parameterExists(-1, root.servoParamName(outputNumber))) {
            return null
        }
        return directControl.getParameterFact(-1, root.servoParamName(outputNumber), false)
    }

    function functionEnumIndex(fact, value) {
        if (!fact || value === undefined) {
            return -1
        }
        for (var index = 0; index < fact.enumValues.length; index++) {
            if (Number(fact.enumValues[index]) === Number(value)) {
                return index
            }
        }
        return -1
    }

    function functionValueText(fact, value) {
        var index = root.functionEnumIndex(fact, value)
        return index >= 0 ? fact.enumStrings[index] + " (" + value + ")" : String(value)
    }

    function requestServoFunctionSave(outputNumber, newValue) {
        var fact = root.servoFunctionFact(outputNumber)
        if (!root.canConfigureServoFunctions || root.functionSaveConfirmation || !fact || fact.readOnly
                || root.functionEnumIndex(fact, newValue) < 0) {
            root.functionSaveStatusText = qsTr("当前不能保存功能：请保持载具连接并上锁，关闭测试，先处理待恢复通道。")
            return
        }
        root.functionSaveConfirmation = functionSaveConfirmationFactory.open({
            outputNumber: outputNumber,
            originalValue: Number(fact.rawValue),
            targetValue: Number(newValue),
            originalText: root.functionValueText(fact, Number(fact.rawValue)),
            targetText: root.functionValueText(fact, Number(newValue)),
            vehicleText: root.sessionVehicleText,
            boundVehicle: root.activeVehicle,
            rebootRequired: fact.vehicleRebootRequired
        })
        if (!root.functionSaveConfirmation) {
            root.functionSaveStatusText = qsTr("确认窗口创建失败，未向飞控写入参数。")
        }
    }

    function confirmServoFunctionSave(outputNumber, expectedOriginal, newValue, boundVehicle) {
        if (!root.canConfigureServoFunctions || root.activeVehicle !== boundVehicle
                || !root.functionSaveConfirmation) {
            root.functionSaveStatusText = qsTr("载具或测试状态已变化，未向飞控写入参数。请重新确认。")
            return
        }
        if (directControl.beginFunctionSave(outputNumber, expectedOriginal, newValue)) {
            root.functionSaveStatusText = qsTr("正在回读 SERVO%1 原功能；确认一致后保存，并再次回读校验。")
                    .arg(outputNumber)
        } else {
            root.functionSaveStatusText = directControl.failureReason
        }
    }

    QGCPopupDialogFactory {
        id: functionSaveConfirmationFactory
        dialogComponent: functionSaveConfirmationComponent
    }

    Component {
        id: functionSaveConfirmationComponent

        QGCPopupDialog {
            id: confirmationDialog
            objectName: "outputFunctionSaveConfirmation"
            title: qsTr("确认保存 SERVO 功能")
            buttons: Dialog.Save | Dialog.Cancel
            required property int outputNumber
            required property int originalValue
            required property int targetValue
            required property string originalText
            required property string targetText
            required property string vehicleText
            required property var boundVehicle
            required property bool rebootRequired
            readonly property var functionFact: root.servoFunctionFact(outputNumber)
            acceptButtonEnabled: root.canConfigureServoFunctions && root.activeVehicle === boundVehicle
                                     && functionFact && Number(functionFact.rawValue) === originalValue

            onAccepted: root.confirmServoFunctionSave(outputNumber, originalValue, targetValue, boundVehicle)
            onClosed: root.functionSaveConfirmation = null

            ColumnLayout {
                width: Math.min(confirmationDialog.maxContentAvailableWidth, ScreenTools.defaultFontPixelWidth * 62)
                spacing: ScreenTools.defaultFontPixelHeight * 0.6

                QGCLabel { text: confirmationDialog.vehicleText }
                QGCLabel { text: root.servoParamName(confirmationDialog.outputNumber); font.bold: true }
                QGCLabel { text: qsTr("当前参数值：%1").arg(confirmationDialog.originalText) }
                QGCLabel { text: qsTr("修改为：%1").arg(confirmationDialog.targetText) }
                QGCLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("点击保存后会修改飞控的输出功能配置。请确认输出口与接线用途一致。")
                }
                QGCLabel {
                    Layout.fillWidth: true
                    visible: confirmationDialog.rebootRequired
                    wrapMode: Text.WordWrap
                    color: confirmationDialog._qgcPal.warningText
                    text: qsTr("此参数需要重启飞控后生效。")
                }
                QGCLabel {
                    Layout.fillWidth: true
                    visible: !confirmationDialog.acceptButtonEnabled
                    wrapMode: Text.WordWrap
                    color: confirmationDialog._qgcPal.warningText
                    text: qsTr("载具、参数或测试状态已变化，请取消后重新确认。")
                }
            }
        }
    }

    function servoFunctionText(outputNumber) {
        if (!root.activeVehicle || !root.activeVehicle.parameterManager.parametersReady) {
            return qsTr("参数不可用")
        }
        var paramName = servoParamName(outputNumber)
        if (!directControl.parameterExists(-1, paramName)) {
            return qsTr("参数缺失")
        }

        var fact = directControl.getParameterFact(-1, paramName, false)
        if (!fact) {
            return qsTr("参数不可用")
        }
        var text = fact.enumOrValueString
        return text && text.length > 0 ? text : fact.rawValue.toString()
    }

    function servoFunctionValue(outputNumber) {
        if (!root.activeVehicle || !root.activeVehicle.parameterManager.parametersReady) {
            return null
        }
        var paramName = servoParamName(outputNumber)
        if (!directControl.parameterExists(-1, paramName)) {
            return null
        }

        var fact = directControl.getParameterFact(-1, paramName, false)
        return fact ? fact.rawValue : null
    }

    function motorNumberFromServoFunction(outputNumber) {
        var functionValue = root.servoFunctionValue(outputNumber)
        var rawValue = functionValue === null ? NaN : Number(functionValue)
        if (!isNaN(rawValue)) {
            if (rawValue >= 33 && rawValue <= 40) {
                return rawValue - 32
            }
            if (rawValue >= 82 && rawValue <= 85) {
                return rawValue - 73
            }
        }

        var functionText = root.servoFunctionText(outputNumber)
        var match = functionText.match(/^Motor\s*([0-9]+)$/)
        return match ? parseInt(match[1]) : -1
    }

    function motorTestTargetForOutput(outputNumber) {
        if (!mappingSettings.useServoFunctionForMotorTest) {
            return outputNumber
        }

        var motorNumber = root.motorNumberFromServoFunction(outputNumber)
        if (motorNumber < 1) {
            root.testStatusText = qsTr("SERVO%1 当前功能是 %2，不是 MotorN，无法按 SERVO_FUNCTION 映射测试。")
                    .arg(outputNumber)
                    .arg(root.servoFunctionText(outputNumber))
            return -1
        }

        return motorNumber
    }

    function storedMappings() {
        try {
            var records = JSON.parse(mappingSettings.vehicleMappings)
            if (!records || typeof records !== "object" || Array.isArray(records)) {
                throw new Error("Invalid mapping records")
            }
            root.mappingStorageValid = true
            return records
        } catch (error) {
            root.mappingStorageValid = false
            root.testStatusText = qsTr("接线记录无法读取，已停用编辑以保留原记录。")
            console.warn("Output wiring records:", error)
            return null
        }
    }

    function loadMappings() {
        var records = root.storedMappings()
        if (records === null) {
            return
        }
        var uid = root.currentVehicleUid
        // A late UID must never silently replace existing records with session edits.
        if (root.recordVehicleUid.length === 0 && root.recordsDirty && records[uid]) {
            root.testStatusText = qsTr("已识别载具 UID，但已有接线记录。本次记录仍保留在当前会话，可先导出，再重新打开查看已有记录。")
            return
        }
        root.recordVehicleUid = uid
        if (root.recordsDirty) {
            root.saveMappings()
            return
        }
        var record = uid.length > 0 ? records[uid] : null
        var ports = []
        var positions = []
        root.legacyMappingsAvailable = false
        for (var output = 1; output <= 16; output++) {
            ports.push(record && Array.isArray(record.ports) && typeof record.ports[output - 1] === "string"
                       ? record.ports[output - 1] : "")
            var index = record && Array.isArray(record.positions) ? Number(record.positions[output - 1]) : 0
            positions.push(Number.isInteger(index) && index >= 0 && index < root.positionNames.length ? index : 0)
            if (mappingSettings["port" + output + "Name"].length > 0
                    || mappingSettings["servo" + output + "Position"] !== 0) {
                root.legacyMappingsAvailable = true
            }
        }
        root.recordPorts = ports
        root.recordPositions = positions
        root.mappingsUpdated()
    }

    function saveMappings() {
        root.recordsDirty = true
        if (root.recordVehicleUid.length === 0) {
            return
        }
        var records = root.storedMappings()
        if (records === null) {
            return
        }
        records[root.recordVehicleUid] = { ports: root.recordPorts, positions: root.recordPositions }
        mappingSettings.vehicleMappings = JSON.stringify(records)
    }

    function portName(outputNumber) {
        return root.recordPorts[outputNumber - 1] || ""
    }

    function setPortName(outputNumber, name) {
        if (!root.canRecordMappings || outputNumber < 1 || outputNumber > 16) {
            return
        }
        var ports = root.recordPorts.slice()
        ports[outputNumber - 1] = name
        root.recordPorts = ports
        root.saveMappings()
        root.mappingsUpdated()
    }

    function positionIndex(outputNumber) {
        return root.recordPositions[outputNumber - 1] || 0
    }

    function setPositionIndex(outputNumber, index) {
        if (!root.canRecordMappings || outputNumber < 1 || outputNumber > 16
                || !Number.isInteger(index) || index < 0 || index >= root.positionNames.length) {
            return
        }
        var positions = root.recordPositions.slice()
        positions[outputNumber - 1] = index
        root.recordPositions = positions
        root.saveMappings()
        root.mappingsUpdated()
    }

    function importLegacyMappings() {
        if (!root.canRecordMappings || !root.legacyMappingsAvailable) {
            return
        }
        if (!root.legacyImportConfirmation) {
            root.legacyImportConfirmation = true
            mappingConfirmTimer.restart()
            root.testStatusText = qsTr("旧记录未区分载具。再次点击导入，将替换本会话的接线记录；请先确认它属于当前载具。")
            return
        }
        var ports = []
        var positions = []
        for (var output = 1; output <= 16; output++) {
            ports.push(mappingSettings["port" + output + "Name"])
            var index = mappingSettings["servo" + output + "Position"]
            positions.push(index >= 0 && index < root.positionNames.length ? index : 0)
        }
        root.recordPorts = ports
        root.recordPositions = positions
        root.saveMappings()
        root.legacyImportConfirmation = false
        root.mappingsUpdated()
        root.testStatusText = qsTr("已导入旧接线记录，未修改飞控配置。")
    }

    function clearMappings() {
        if (!root.canRecordMappings) {
            return
        }
        if (!root.clearMappingsConfirmation) {
            root.clearMappingsConfirmation = true
            mappingConfirmTimer.restart()
            root.testStatusText = qsTr("再次点击清空，将清除当前载具的接线记录。")
            return
        }
        var ports = []
        var positions = []
        for (var output = 1; output <= 16; output++) {
            ports.push("")
            positions.push(0)
        }
        root.recordPorts = ports
        root.recordPositions = positions
        root.saveMappings()
        root.clearMappingsConfirmation = false
        root.mappingsUpdated()
        root.testStatusText = qsTr("已清空当前载具的接线记录，未修改飞控配置。")
    }

    function csvEscape(value) {
        var text = value === null || value === undefined ? "" : value.toString()
        return "\"" + text.replace(/"/g, "\"\"") + "\""
    }

    function csvLine(values) {
        var fields = []
        for (var i = 0; i < values.length; i++) {
            fields.push(root.csvEscape(values[i]))
        }
        return fields.join(",")
    }

    function positionText(outputNumber) {
        var index = root.positionIndex(outputNumber)
        return index >= 0 && index < root.positionNames.length ? root.positionNames[index] : ""
    }

    function exportCsvText() {
        var lines = []
        lines.push(root.csvLine(["DeepShark Output Test and Wiring Records"]))
        lines.push(root.csvLine(["Vehicle ID", root.recordVehicleId]))
        lines.push(root.csvLine(["Vehicle UID", root.recordVehicleUid.length > 0 ? root.recordVehicleUid : "Not reported / session only"]))
        lines.push(root.csvLine(["Export Time", new Date().toLocaleString()]))
        lines.push(root.csvLine(["Motor Test Mode", mappingSettings.useServoFunctionForMotorTest ? "SERVO_FUNCTION mapping" : "Test number"]))
        lines.push(root.csvLine(["Thruster Direct SERVO PWM", mappingSettings.directServoPwm]))
        lines.push(root.csvLine(["Thruster Direct Seconds", mappingSettings.directServoSeconds]))
        lines.push(root.csvLine(["Servo Jog PWM", mappingSettings.servoJogPwm]))
        lines.push(root.csvLine(["Servo Jog Seconds", mappingSettings.servoJogSeconds]))
        lines.push(root.csvLine([""]))
        lines.push(root.csvLine([
            "SERVO Output",
            "SERVO_FUNCTION",
            "Mapped Motor Number",
            "Motor Test Target If Mapping Mode",
            "Board Connector",
            "Observed Thruster Position",
            "Notes"
        ]))

        for (var output = 1; output <= 16; output++) {
            var motorNumber = root.motorNumberFromServoFunction(output)
            lines.push(root.csvLine([
                "SERVO" + output,
                root.servoFunctionText(output),
                motorNumber > 0 ? "Motor" + motorNumber : "",
                motorNumber > 0 ? motorNumber : "",
                root.portName(output),
                root.positionText(output),
                motorNumber > 0 ? "" : "SERVO_FUNCTION is not MotorN"
            ]))
        }

        lines.push(root.csvLine([""]))
        lines.push(root.csvLine(["Normal Motor Test", "Can run either by test number or by SERVO_FUNCTION -> MotorN mapping."]))
        lines.push(root.csvLine(["Thruster Direct SERVO Test", "Uses the physical SERVO output number and returns to 1500 PWM."]))
        lines.push(root.csvLine(["Servo Jog Test", "Uses the physical SERVO output number and returns to SERVOx_TRIM."]))
        return lines.join("\n") + "\n"
    }

    function exportMappingCsv(filename) {
        if (exportController.saveCsv(filename, root.exportCsvText())) {
            root.testStatusText = qsTr("已导出映射 CSV：%1").arg(filename)
        } else {
            root.testStatusText = exportController.lastError
        }
    }

    function recoveryMatchesSession() {
        if (!mappingSettings.recoveryPending || !root.activeVehicle) {
            return false
        }

        if (root.recoveryVehicleObject !== null) {
            return root.recoveryVehicleObject === root.activeVehicle
        }

        if (mappingSettings.recoveryVehicleUid.length > 0) {
            return Number(root.activeVehicle.vehicleUID) > 0
                    && root.activeVehicle.vehicleUIDStr === mappingSettings.recoveryVehicleUid
        }

        return root.recoveryManualConfirmed
    }

    function confirmRecoveryVehicleManually() {
        if (!root.recoveryManualConfirmAvailable) {
            return
        }
        if (!root.recoveryManualConfirmation) {
            var fact = directControl.getParameterFact(-1, root.servoParamName(mappingSettings.recoveryOutput), false)
            root.recoveryManualConfirmation = true
            root.directServoStatusText = qsTr("恢复记录没有序列号可比对。记录：载具编号 %1，SERVO%2，原功能 %3，记录于 %4。当前：载具编号 %5，该路功能现值 %6。确认是同一台机器人后，再次点击“确认是这台机器人”。")
                    .arg(mappingSettings.recoveryVehicleId)
                    .arg(mappingSettings.recoveryOutput)
                    .arg(mappingSettings.recoveryFunction)
                    .arg(mappingSettings.recoveryTimestamp)
                    .arg(root.activeVehicle.id)
                    .arg(fact ? fact.rawValue : qsTr("未知"))
            manualRecoveryConfirmTimer.restart()
            return
        }
        manualRecoveryConfirmTimer.stop()
        root.recoveryManualConfirmation = false
        root.recoveryManualConfirmed = true
        root.directServoStatusText = qsTr("已人工确认恢复记录属于当前机器人。请保持飞控上锁并使用“恢复异常通道”。")
    }

    function saveRecoveryJournal(outputNumber, originalFunction) {
        mappingSettings.recoveryVehicleId = root.activeVehicle.id
        mappingSettings.recoveryVehicleUid = Number(root.activeVehicle.vehicleUID) > 0
                ? root.activeVehicle.vehicleUIDStr
                : ""
        mappingSettings.recoveryOutput = outputNumber
        mappingSettings.recoveryFunction = originalFunction
        mappingSettings.recoveryNeutralPwm = root.directServoNeutralPwm
        mappingSettings.recoveryOperation = root.directServoOperation
        mappingSettings.recoveryTimestamp = new Date().toLocaleString()
        mappingSettings.recoveryPending = true
        root.recoveryVehicleObject = root.activeVehicle
        root.recoveryManualConfirmed = false
        root.recoveryManualConfirmation = false
        root.storeRecoveryJournal()
    }

    // Settings writes assigned properties only after a delay, and sync() does not flush them. The
    // recovery record must not wait for that: a crash in between would leave an output disabled with
    // no record of it.
    function storeRecoveryJournal() {
        const keys = ["recoveryPending", "recoveryVehicleId", "recoveryVehicleUid", "recoveryOutput",
                      "recoveryFunction", "recoveryTimestamp", "recoveryNeutralPwm", "recoveryOperation"]
        for (const key of keys) {
            mappingSettings.setValue(key, mappingSettings[key])
        }
        mappingSettings.sync()
    }

    // The function of one output was written back and read back. That says nothing about the others. A flight
    // controller that is started while a motor function has no output gives that function to a default
    // output, in memory only; with the original output restored, two outputs then carry the same motor.
    function outputsSharingFunctionOf(output) {
        var fact = directControl.getParameterFact(-1, root.servoParamName(output), false)
        var sharedFunction = fact ? Number(fact.rawValue) : NaN
        var others = []
        if (!directControl.functionRefusesDirectPwm(sharedFunction)) {
            return others
        }
        for (var other = 1; other <= 16; other++) {
            var otherFact = other === output ? null : directControl.getParameterFact(-1, root.servoParamName(other), false)
            if (otherFact && Number(otherFact.rawValue) === sharedFunction) {
                others.push(other)
            }
        }
        return others
    }

    function updateOutputMappingNotice(output, afterRecovery, readBack) {
        var others = root.outputsSharingFunctionOf(output)
        if (others.length > 0) {
            var fact = directControl.getParameterFact(-1, root.servoParamName(output), false)
            root.outputMappingNotice = qsTr("注意：SERVO%1 与 SERVO%2 现在是同一个电机功能（%3），会输出同一个信号。飞控如果在 SERVO%1 被禁用的状态下启动过，会把这个电机功能临时补到默认输出上。请在推进器不带负载的情况下重启飞控，再核对全部输出功能：临时补上的会消失；仍然存在的，改回它原来的功能。核对前不要下水。")
                    .arg(output)
                    .arg(others.join("、SERVO"))
                    .arg(fact ? fact.rawValue : "")
            // The tool stays open with this on it.
            root.closeAfterSafeShutdown = false
        } else if (afterRecovery && !readBack) {
            root.outputMappingNotice = qsTr("SERVO%1 的回中和功能参数已恢复。正在重新读取其他输出的功能，尚未核对；请等这一行更新。").arg(output)
        } else if (afterRecovery) {
            root.outputMappingNotice = qsTr("SERVO%1 的回中和功能参数已恢复；刚重新读回的其他输出里没有相同的电机功能。这只说明这一路：如果出现异常期间飞控重启或掉电过，请再核对一遍全部输出功能，必要时重启飞控后复查，然后再下水。").arg(output)
        } else {
            root.outputMappingNotice = ""
        }
    }

    function checkOutputMapping(output, afterRecovery) {
        root.outputMappingNoticeOutput = output
        root.outputMappingNoticeAfterRecovery = afterRecovery
        root.updateOutputMappingNotice(output, afterRecovery, false)
        if (afterRecovery) {
            // What led to a recovery may have been a restart of the flight controller, and the functions of
            // the other outputs were read before it. Ask again; the notice is worked out again when the
            // answers are in. Until the operator has read it the tool does not close by itself.
            root.closeAfterSafeShutdown = false
            directControl.refreshOutputFunctions()
        }
    }

    function clearRecoveryJournal() {
        discardRecoveryConfirmTimer.stop()
        root.recoveryDiscardConfirmation = false
        recoveryOperatorConfirmTimer.stop()
        root.recoveryOperatorConfirmation = false
        root.recoveryForceConfirmation = false
        mappingSettings.recoveryPending = false
        mappingSettings.recoveryVehicleId = -1
        mappingSettings.recoveryVehicleUid = ""
        mappingSettings.recoveryOutput = -1
        mappingSettings.recoveryFunction = -1
        mappingSettings.recoveryTimestamp = ""
        mappingSettings.recoveryNeutralPwm = 1500
        mappingSettings.recoveryOperation = "thruster"
        root.recoveryVehicleObject = null
        root.recoveryManualConfirmed = false
        root.recoveryManualConfirmation = false
        manualRecoveryConfirmTimer.stop()
        root.storeRecoveryJournal()
    }

    function directServoBusy() {
        return directControl.busy
    }

    function directServoStateText() {
        switch (root.directServoState) {
        case "waitingBackup": return qsTr("回读原功能")
        case "waitingDisable": return qsTr("等待禁用确认")
        case "disableSettling": return qsTr("准备输出")
        case "waitingTestAck": return qsTr("等待测试回执")
        case "testing": return qsTr("正在输出")
        case "waitingNeutralAck": return qsTr("等待回中回执")
        case "confirmingNeutral": return qsTr("核对输出回中")
        case "waitingRestore": return qsTr("正在恢复参数")
        case "savingFunction": return qsTr("保存并回读功能")
        case "recoveryNeeded": return qsTr("需要人工恢复")
        default: return qsTr("空闲")
        }
    }

    function recoveryJournalCanBeDiscarded() {
        // A cached function value cannot prove that a previous PWM was neutralized.
        return !root.recoveryMatchesSession()
    }

    function directServoOperationText() {
        return root.directServoOperation === "servo" ? qsTr("舵机点动") : qsTr("推进器 PWM 直测")
    }

    function startDirectServoTest(outputNumber) {
        if (root.testMode === 1) {
            root.startPhysicalServoTest(outputNumber, false)
        }
    }

    function startServoJogTest(outputNumber) {
        if (root.testMode === 2) {
            root.startPhysicalServoTest(outputNumber, true)
        }
    }

    function startPhysicalServoTest(outputNumber, servoJog) {
        if (!root.canStartDirectTest || root.testMode !== (servoJog ? 2 : 1)) {
            root.directServoStatusText = qsTr("无法开始：请启用对应测试模式、保持载具在线且上锁，并先完成正在执行的测试或恢复。")
            return
        }

        root.directServoOperation = servoJog ? "servo" : "thruster"
        root.directServoTestPwm = servoJog ? mappingSettings.servoJogPwm : mappingSettings.directServoPwm
        root.directServoTestSeconds = servoJog ? mappingSettings.servoJogSeconds : mappingSettings.directServoSeconds
        root.directServoNeutralPwm = 1500
        if (root.directServoTestPwm < (servoJog ? 800 : 1000) || root.directServoTestPwm > 2200
                || root.directServoTestSeconds < 1 || root.directServoTestSeconds > 5) {
            root.directServoStatusText = qsTr("PWM 或测试时长超出允许范围，请先调整。")
            return
        }

        var fact = directControl.getParameterFact(-1, root.servoParamName(outputNumber), false)
        if (!fact) {
            root.directServoStatusText = qsTr("缺少 %1，无法开始%2。")
                    .arg(root.servoParamName(outputNumber))
                    .arg(root.directServoOperationText())
            return
        }

        if (servoJog) {
            var trimParamName = root.servoTrimParamName(outputNumber)
            var trimFact = directControl.getParameterFact(-1, trimParamName, false)
            var trimPwm = trimFact ? Number(trimFact.rawValue) : NaN
            if (isNaN(trimPwm) || trimPwm < 800 || trimPwm > 2200) {
                root.directServoStatusText = qsTr("%1 缺失或无效，无法保证点动后安全回中。").arg(trimParamName)
                return
            }

            var minParamName = root.servoLimitParamName(outputNumber, "MIN")
            var maxParamName = root.servoLimitParamName(outputNumber, "MAX")
            var minFact = directControl.getParameterFact(-1, minParamName, false)
            var maxFact = directControl.getParameterFact(-1, maxParamName, false)
            var minPwm = minFact ? Number(minFact.rawValue) : NaN
            var maxPwm = maxFact ? Number(maxFact.rawValue) : NaN
            if (!isFinite(minPwm) || !isFinite(maxPwm) || minPwm > maxPwm || trimPwm < minPwm || trimPwm > maxPwm) {
                root.directServoStatusText = qsTr("%1/%2 缺失或无效，无法安全限制点动范围。")
                        .arg(minParamName)
                        .arg(maxParamName)
                return
            }
            if (root.directServoTestPwm < minPwm || root.directServoTestPwm > maxPwm) {
                root.directServoStatusText = qsTr("当前舵机点动 PWM=%1 超出 SERVO%2 范围 %3-%4，请先调整 PWM。")
                        .arg(root.directServoTestPwm)
                        .arg(outputNumber)
                        .arg(minPwm)
                        .arg(maxPwm)
                return
            }
            root.directServoNeutralPwm = Math.round(trimPwm)
        }

        var originalFunction = Number(fact.rawValue)
        if (!Number.isInteger(originalFunction) || originalFunction < 0) {
            root.directServoStatusText = qsTr("%1 当前值无效，无法安全备份。").arg(root.servoParamName(outputNumber))
            return
        }

        var outputReportProblem = directControl.outputReportProblem()
        if (outputReportProblem !== "" && !root.restrictionsOff) {
            root.directServoStatusText = qsTr("%1。未开始%2，未修改飞控参数。")
                    .arg(outputReportProblem)
                    .arg(root.directServoOperationText())
            return
        }

        root.testSessionOwned = true
        root.directServoFact = fact
        root.directServoStatusText = qsTr("%1：正在从飞控确认 %2 原功能。")
                .arg(root.directServoOperationText())
                .arg(root.servoParamName(outputNumber))
        if (!directControl.beginTest(outputNumber,
                                     originalFunction,
                                     root.directServoTestPwm,
                                     root.directServoNeutralPwm,
                                     Math.max(1, root.directServoTestSeconds) * 1000)) {
            root.clearRecoveryJournal()
            root.directServoFact = null
            root.directServoStatusText = directControl.failureReason || qsTr("直接输出状态初始化失败，未修改飞控参数。")
        } else if (outputReportProblem !== "") {
            root.directServoStatusText = qsTr("%1。安全限制已关闭，照常开始%2；结束后软件无法自动确认回中，请用“人工确认完成”收尾。")
                    .arg(outputReportProblem)
                    .arg(root.directServoOperationText())
        }
    }

    function recoverPendingServoFunction() {
        if (!mappingSettings.recoveryPending) {
            root.directServoStatusText = qsTr("没有待恢复记录。")
            return
        }

        if (!root.vehicleReady || !root.recoveryMatchesSession() || root.directServoBusy() || root.runningOutput !== -1
                || root.armingRequested || root.disarmRequested || root.disarmUnconfirmed) {
            root.directServoStatusText = qsTr("恢复记录与当前载具不匹配，已拒绝写入。")
            return
        }

        if (!root.recoveryPermitted) {
            root.directServoStatusText = qsTr("这条恢复记录没有序列号，只能凭人工确认恢复，而人工确认恢复已在系统设置里关闭；未写入飞控。")
            return
        }

        if (root.activeVehicle.armed) {
            root.directServoStatusText = qsTr("请先上锁，再恢复异常通道。")
            return
        }

        if (root.activeVehicle.parameterManager.pendingWrites) {
            root.directServoStatusText = qsTr("飞控仍有参数写入任务，请等待完成后再恢复。")
            return
        }

        var fact = directControl.getParameterFact(-1, root.servoParamName(mappingSettings.recoveryOutput), false)
        if (!fact) {
            root.directServoStatusText = qsTr("缺少恢复记录对应的参数，无法恢复。")
            return
        }

        var currentFunction = Number(fact.rawValue)
        if (currentFunction !== 0 && currentFunction !== mappingSettings.recoveryFunction) {
            root.directServoStatusText = qsTr("SERVO%1 当前值为 %2，不是 Disabled，也不是记录原值；为避免覆盖新配置，已拒绝恢复。")
                    .arg(mappingSettings.recoveryOutput)
                    .arg(currentFunction)
            return
        }

        var operatorConfirms = directControl.recoveryNeedsOperatorConfirmation(mappingSettings.recoveryFunction)
        if (operatorConfirms && !root.recoveryOperatorConfirmation) {
            root.recoveryOperatorConfirmation = true
            root.directServoStatusText = qsTr("SERVO%1 的原功能（%2）不在软件能自动恢复的范围内（只有电机功能恢复后飞控会拒绝迟到的直接 PWM 指令）。软件会先回中并核对飞控上报的输出，但无法排除更早的测试指令还在路上、恢复之后才到达。请确认这一路所接的设备已经停止，再点一次“确认设备已停止并恢复”。")
                    .arg(mappingSettings.recoveryOutput)
                    .arg(mappingSettings.recoveryFunction)
            recoveryOperatorConfirmTimer.restart()
            return
        }
        recoveryOperatorConfirmTimer.stop()
        root.recoveryOperatorConfirmation = false

        root.directServoOperation = mappingSettings.recoveryOperation
        root.directServoNeutralPwm = mappingSettings.recoveryNeutralPwm
        root.testSessionOwned = true
        root.directServoFact = fact
        root.recoverySessionActive = true
        root.directServoStatusText = qsTr("正在恢复异常通道 SERVO%1。").arg(mappingSettings.recoveryOutput)
        if (!directControl.beginRecovery(mappingSettings.recoveryOutput,
                                         mappingSettings.recoveryFunction,
                                         operatorConfirms ? qsTr("人工恢复，操作员确认设备已停止") : qsTr("人工恢复"),
                                         root.directServoNeutralPwm, operatorConfirms)) {
            root.recoverySessionActive = false
            root.directServoFact = null
            root.directServoStatusText = directControl.failureReason || qsTr("恢复状态初始化失败，未修改飞控参数。")
        }
    }

    // With the safety restrictions off the operator may finish a recovery on their own word: neutral is sent,
    // and the function is written back once the flight controller has accepted it, without waiting for its
    // output reports. Whether the record belongs to this vehicle is also the operator's to say.
    function forceCompleteRecovery() {
        if (!mappingSettings.recoveryPending || !root.restrictionsOff) {
            return
        }
        if (!root.vehicleReady || root.directServoBusy() || root.activeVehicle.armed) {
            root.directServoStatusText = qsTr("请保持载具在线且上锁，并等当前操作结束，再人工确认完成。")
            return
        }
        var fact = directControl.getParameterFact(-1, root.servoParamName(mappingSettings.recoveryOutput), false)
        if (!fact) {
            root.directServoStatusText = qsTr("缺少恢复记录对应的参数，无法恢复。")
            return
        }
        if (!root.recoveryForceConfirmation) {
            root.recoveryForceConfirmation = true
            root.directServoStatusText = qsTr("“人工确认完成”不等飞控上报输出：软件发一次回中，飞控接受后就把 SERVO%1 写回功能 %2 并清掉记录。请确认这一路所接的设备已经停止，再点一次。")
                    .arg(mappingSettings.recoveryOutput)
                    .arg(mappingSettings.recoveryFunction)
            recoveryOperatorConfirmTimer.restart()
            return
        }
        recoveryOperatorConfirmTimer.stop()
        root.recoveryForceConfirmation = false
        root.recoveryOperatorConfirmation = false

        root.directServoOperation = mappingSettings.recoveryOperation
        root.directServoNeutralPwm = mappingSettings.recoveryNeutralPwm
        root.testSessionOwned = true
        root.directServoFact = fact
        root.recoverySessionActive = true
        root.directServoStatusText = qsTr("正在按人工确认恢复 SERVO%1。").arg(mappingSettings.recoveryOutput)
        if (!directControl.beginOperatorRecovery(mappingSettings.recoveryOutput,
                                                 mappingSettings.recoveryFunction,
                                                 qsTr("人工确认完成（安全限制已关闭，未等飞控上报输出）"),
                                                 root.directServoNeutralPwm)) {
            root.recoverySessionActive = false
            root.directServoFact = null
            root.directServoStatusText = directControl.failureReason || qsTr("无法开始恢复：飞控仍有未完成的参数写入或 PWM 指令，或 SERVO%1 当前既不是 Disabled 也不是记录的原功能。")
                    .arg(mappingSettings.recoveryOutput)
        }
    }

    function discardRecoveryJournal() {
        if (!mappingSettings.recoveryPending || root.directServoBusy()) {
            return
        }

        if (!root.recoveryJournalCanBeDiscarded()) {
            root.directServoStatusText = qsTr("当前载具存在未确认的输出恢复，不能忽略记录；请先执行恢复并确认回中。")
            return
        }

        if (!root.recoveryDiscardConfirmation) {
            root.recoveryDiscardConfirmation = true
            root.directServoStatusText = qsTr("再次点击“确认忽略记录”才会清除本机恢复记录；不会写入飞控。")
            discardRecoveryConfirmTimer.restart()
            return
        }

        root.clearRecoveryJournal()
        directControl.reset()
        root.directServoFact = null
        root.directServoStatusText = qsTr("已忽略本机旧恢复记录；未向飞控写入任何参数。")
    }

    function abortDirectServoTest(reason) {
        if (!root.directServoBusy()) {
            return
        }

        directControl.abort(reason)
    }

    function finishDirectServoTest() {
        directControl.finishTest()
    }

    function testOutput(outputNumber) {
        if (!root.canStartMotorTest) {
            root.testStatusText = qsTr("无法开始 Motor Test：请启用测试、准备解锁，并等待当前测试、恢复及冷却完成。")
            return
        }

        var motorTarget = root.motorTestTargetForOutput(outputNumber)
        if (motorTarget < 1) {
            return
        }

        root.testSessionOwned = true
        root.runningOutput = outputNumber
        root.runningMotorTarget = motorTarget
        root.motorTestPulsesRemaining = Math.max(1, Math.ceil(mappingSettings.testSeconds * 1000 / motorTestPulseTimer.interval))
        root.testStatusText = mappingSettings.useServoFunctionForMotorTest ?
                    qsTr("正在按 SERVO%1_FUNCTION 映射点动 Motor%2。").arg(outputNumber).arg(motorTarget) :
                    qsTr("正在按测试编号点动 Motor Test %1。").arg(motorTarget)
        root.sendMotorTestPulse()
        if (root.motorTestPulsesRemaining > 0 && !motorTestPulseTimer.running) {
            motorTestPulseTimer.start()
        }
    }

    function sendMotorTestPulse() {
        if (!root.vehicleReady || root.runningOutput === -1 || root.directServoBusy() || mappingSettings.recoveryPending) {
            motorTestPulseTimer.stop()
            if (root.runningOutput !== -1) {
                root.finishMotorPulse(true)
                root.testStatusText = qsTr("活动载具发生变化，Motor Test 已停止。")
            }
            return
        }

        if (!root.activeVehicle.armed) {
            root.finishMotorPulse(false)
            root.testStatusText = qsTr("飞控已上锁，点动已停止。点击“准备下一次测试”重新解锁。")
            return
        }

        if (root.motorTestPulsesRemaining <= 0) {
            root.finishMotorPulse(true)
            return
        }

        root.activeVehicle.motorTest(root.runningMotorTarget, mappingSettings.testPercent, 0, false)
        root.motorTestPulsesRemaining--
    }

    function finishMotorPulse(sendStopCommand) {
        if (motorTestPulseTimer.running) {
            motorTestPulseTimer.stop()
        }

        var outputNumber = root.runningOutput
        var motorTarget = root.runningMotorTarget
        root.runningOutput = -1
        root.runningMotorTarget = -1
        root.motorTestPulsesRemaining = 0

        if (sendStopCommand && root.activeVehicle && motorTarget !== -1) {
            root.activeVehicle.motorTest(motorTarget, 0, 0, false)
        }

        root.startCooldown(11)
        root.testStatusText = mappingSettings.useServoFunctionForMotorTest && outputNumber !== motorTarget ?
                    qsTr("SERVO%1 -> Motor%2 点动结束，正在请求上锁。").arg(outputNumber).arg(motorTarget) :
                    qsTr("Motor Test %1 点动结束，正在请求上锁。").arg(motorTarget)
        root.requestSafeDisarm(root.closeAfterSafeShutdown, qsTr("点动测试已结束"))
    }

    function beginSafeShutdown(closeAfter, reason) {
        root.closeAfterSafeShutdown = root.closeAfterSafeShutdown || closeAfter
        root.testEnabled = false

        if (directControl.functionSaveBusy) {
            root.functionSaveStatusText = qsTr("正在等待参数保存结果，完成后再结束。")
            directControl.abort(reason)
            return
        }

        if (root.armCommandPending) {
            root.stopAfterArming = true
            root.testStatusText = qsTr("正在等待解锁请求结束，随后将请求上锁。")
            return
        }
        if (root.armingRequested) {
            root.requestSafeDisarm(root.closeAfterSafeShutdown, reason, true)
            return
        }
        root.armingRequested = false

        if (root.directServoBusy()) {
            root.directServoStatusText = qsTr("正在安全回中并恢复参数，完成后将请求飞控上锁。")
            root.abortDirectServoTest(reason)
            return
        }

        if (root.runningOutput !== -1) {
            root.finishMotorPulse(true)
            return
        }

        if (root.testSessionOwned || root.vehicleArmingUnconfirmed) {
            root.requestSafeDisarm(root.closeAfterSafeShutdown, reason)
        } else if (root.closeAfterSafeShutdown) {
            root.closeAfterSafeShutdown = false
            root.close()
        }
    }

    function requestSafeDisarm(closeAfter, reason, forceCommand) {
        root.closeAfterSafeShutdown = root.closeAfterSafeShutdown || closeAfter
        root.testEnabled = false
        root.armingRequested = false

        if (!root.activeVehicle) {
            root.disarmRequested = false
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("载具连接已丢失，无法确认上锁；工具保持打开。")
            return
        }

        if (!root.activeVehicle.armed && !forceCommand && !root.disarmUnconfirmed && !root.vehicleArmingUnconfirmed) {
            root.completeSafeShutdown()
            return
        }

        if (root.activeVehicle.armDisarmCommandPending() && !root.disarmRequested) {
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("飞控仍在等待另一条解锁或上锁指令的回执；请等待请求结束后再次点击“安全结束”。")
            return
        }

        if (root.disarmRequested) {
            return
        }

        root.disarmRequested = true
        root.disarmAckConfirmed = false
        root.disarmUnconfirmed = true
        root.testStatusText = qsTr("%1，正在请求飞控上锁。").arg(reason)
        safeDisarmTimeout.restart()
        root.activeVehicle.armed = false
    }

    function completeSafeShutdown() {
        if (root.armCommandPending || root.vehicleArmingUnconfirmed) {
            return
        }
        safeDisarmTimeout.stop()
        root.disarmRequested = false
        root.armingRequested = false
        root.stopAfterArming = false
        root.disarmAckConfirmed = false
        root.disarmUnconfirmed = false
        root.testEnabled = false
        if (root.directServoBusy() || mappingSettings.recoveryPending) {
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("飞控已确认上锁，输出恢复尚未完成；请处理恢复记录。")
            return
        }
        root.testSessionOwned = false
        root.runningOutput = -1
        root.runningMotorTarget = -1
        root.motorTestPulsesRemaining = 0
        root.testStatusText = qsTr("测试已安全结束：输出已停止，飞控已确认上锁。")

        if (root.closeAfterSafeShutdown) {
            root.closeAfterSafeShutdown = false
            Qt.callLater(function() {
                root.close()
            })
        }
    }

    function prepareForNextTest() {
        if (!root.canPrepareMotorTest) {
            root.testStatusText = qsTr("无法准备 Motor Test：请启用该模式、保持载具在线，并先完成当前测试或恢复。")
            return
        }

        root.testSessionOwned = true
        if (root.activeVehicle.armed) {
            root.armingRequested = false
            root.testStatusText = root.cooldownRemaining > 0 ?
                        qsTr("已解锁，请等待冷却倒计时结束后再点动。") :
                        qsTr("已准备，可以点动一路输出。")
            return
        }

        root.armingRequested = true
        root.armCommandPending = true
        root.stopAfterArming = false
        root.testStatusText = qsTr("正在请求解锁。解锁成功后会等待冷却，再允许点动。")
        root.activeVehicle.armed = true
    }

    function completeArming() {
        if (root.armCommandPending || !root.activeVehicle || !root.activeVehicle.armed) {
            return
        }
        root.armingRequested = false
        root.startCooldown(11)
        root.testStatusText = qsTr("已解锁。请等待冷却倒计时结束后再点动。")
    }

    function startCooldown(seconds) {
        root.cooldownRemaining = Math.max(root.cooldownRemaining, seconds)
        if (!testCooldown.running) {
            testCooldown.start()
        }
    }

    Timer {
        id:         motorTestPulseTimer
        interval:   50
        repeat:     true
        onTriggered: root.sendMotorTestPulse()
    }

    Timer {
        id:         safeDisarmTimeout
        interval:   5000
        repeat:     false
        onTriggered: {
            if (root.disarmAckConfirmed && root.activeVehicle && !root.activeVehicle.armed
                    && !root.vehicleArmingUnconfirmed) {
                root.completeSafeShutdown()
                return
            }

            root.disarmRequested = false
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("未确认飞控上锁，工具保持打开。请在主界面手动上锁并确认后再关闭。")
        }
    }

    Timer {
        id:         discardRecoveryConfirmTimer
        interval:   5000
        repeat:     false
        onTriggered: root.recoveryDiscardConfirmation = false
    }

    Timer {
        id:         recoveryOperatorConfirmTimer
        interval:   15000
        repeat:     false
        onTriggered: {
            root.recoveryOperatorConfirmation = false
            root.recoveryForceConfirmation = false
        }
    }


    Timer {
        id:         manualRecoveryConfirmTimer
        interval:   20000 // Long enough to read and compare the record.
        repeat:     false
        onTriggered: root.recoveryManualConfirmation = false
    }

    Timer {
        id:         testCooldown
        interval:   1000
        repeat:     true
        onTriggered: {
            if (root.cooldownRemaining > 0) {
                root.cooldownRemaining--
            }

            if (root.cooldownRemaining <= 0) {
                root.cooldownRemaining = 0
                if (root.testEnabled && root.testMode === 0 && !root.disarmRequested
                        && !root.armingRequested && !root.directServoBusy() && !mappingSettings.recoveryPending) {
                    root.testStatusText = root.activeVehicle && !root.activeVehicle.armed ?
                                qsTr("冷却结束。飞控当前已上锁，点击“准备下一次测试”。") :
                                qsTr("可以继续测试下一路输出。")
                }
                stop()
            }
        }
    }

    Connections {
        target: root.activeVehicle

        function onArmingStateUnconfirmedChanged() {
            if (!root.vehicleArmingUnconfirmed && root.disarmRequested && root.disarmAckConfirmed
                    && !root.activeVehicle.armed) {
                root.completeSafeShutdown()
            }
        }

        function onMavCommandResult(vehicleId, targetComponent, command, ackResult, failureCode) {
            if (!root.activeVehicle || vehicleId !== root.activeVehicle.id) {
                return
            }

            if (command === root.armDisarmCommand && (root.armCommandPending || root.disarmRequested)) {
                if (root.armCommandPending) {
                    root.armCommandPending = false
                    if (root.stopAfterArming) {
                        root.stopAfterArming = false
                        // Even a lost ARM ACK can mean the vehicle accepted it.
                        // Send DISARM after that queue entry has reached its terminal result.
                        root.requestSafeDisarm(root.closeAfterSafeShutdown, qsTr("结束解锁准备"), true)
                    } else if (ackResult !== 0 || failureCode !== 0) {
                        root.armingRequested = false
                        root.testEnabled = false
                        root.testStatusText = qsTr("未确认解锁成功。请检查安全开关、模式和故障信息。")
                        if (failureCode !== 0) {
                            root.requestSafeDisarm(false, qsTr("解锁结果不明确"), true)
                        }
                    } else {
                        root.completeArming()
                    }
                } else if (root.disarmRequested) {
                    if (ackResult !== 0 || failureCode !== 0) {
                        safeDisarmTimeout.stop()
                        root.disarmRequested = false
                        root.closeAfterSafeShutdown = false
                        root.testStatusText = qsTr("未确认上锁指令成功，窗口保持打开。请在主界面手动上锁并检查故障信息。")
                    } else {
                        root.disarmAckConfirmed = true
                        if (!root.activeVehicle.armed) {
                            root.completeSafeShutdown()
                        }
                    }
                }
                return
            }

            if (command !== root.motorTestCommand) {
                return
            }

            if (ackResult !== 0 && root.runningOutput !== -1) {
                root.finishMotorPulse(true)
                root.testStatusText = root.activeVehicle && root.activeVehicle.armed
                        ? qsTr("飞控拒绝 Motor Test，已停止输出并请求上锁。")
                        : qsTr("飞控拒绝 Motor Test，已停止输出并确认上锁。")
            }
        }

        function onArmedChanged(armed) {
            if (!armed && root.disarmRequested) {
                if (root.disarmAckConfirmed) {
                    root.completeSafeShutdown()
                }
                return
            }

            if (!armed && root.disarmUnconfirmed && !root.armCommandPending) {
                root.completeSafeShutdown()
                return
            }

            if (!root.vehicleContextMatches) {
                return
            }

            if (armed) {
                if (root.armingRequested) {
                    root.completeArming()
                } else if (root.testEnabled && root.testMode === 0 && root.cooldownRemaining === 0) {
                    root.testStatusText = qsTr("已解锁，可以点动一路输出。")
                } else if (root.testMode !== 0) {
                    root.testEnabled = false
                    root.testStatusText = qsTr("直接输出测试要求飞控上锁，测试已停用。")
                }
                return
            }

            if (!root.armCommandPending) {
                root.armingRequested = false
            }
            if (motorTestPulseTimer.running) {
                motorTestPulseTimer.stop()
            }
            root.runningOutput = -1
            root.runningMotorTarget = -1
            root.motorTestPulsesRemaining = 0
            if (root.testEnabled) {
                root.testStatusText = qsTr("飞控已上锁。点击“准备下一次测试”重新解锁。")
            }
        }

        function onVehicleUIDChanged() {
            if (root.recordVehicleUid.length === 0 && root.currentVehicleUid.length > 0) {
                root.loadMappings()
            }
            if (mappingSettings.recoveryPending
                    && root.recoveryVehicleObject === root.activeVehicle
                    && mappingSettings.recoveryVehicleUid.length === 0
                    && Number(root.activeVehicle.vehicleUID) > 0) {
                mappingSettings.recoveryVehicleUid = root.activeVehicle.vehicleUIDStr
                root.storeRecoveryJournal()
            }
        }
    }

    Connections {
        target: root.activeVehicle ? root.activeVehicle.vehicleLinkManager : null
        function onCommunicationLostChanged(communicationLost) {
            if (communicationLost && root.testSessionOwned) {
                root.beginSafeShutdown(false, qsTr("飞控心跳中断"))
            }
        }
    }

    Timer {
        id: mappingConfirmTimer
        interval: 5000
        onTriggered: {
            root.legacyImportConfirmation = false
            root.clearMappingsConfirmation = false
        }
    }

    QGCFileDialog {
        id:             exportFileDialog
        title:          qsTr("导出接线记录")
        folder:         root.appSettings ? root.appSettings.parameterSavePath : ""
        nameFilters:    [ qsTr("CSV 文件 (*.csv)"), qsTr("所有文件 (*)") ]
        defaultSuffix:  "csv"

        onAcceptedForSave: (file) => {
            root.exportMappingCsv(file)
            close()
        }
    }

    ColumnLayout {
        id:         toolLayout
        width:      Math.min(root.maxContentAvailableWidth, ScreenTools.defaultFontPixelWidth * 112)
        height:     root.maxContentAvailableHeight
        spacing:    ScreenTools.defaultFontPixelHeight * 0.45

        readonly property bool settingsEditable: root.vehicleReady
                                                    && !root.directServoBusy()
                                                    && root.runningOutput === -1
                                                    && !root.armingRequested && !root.disarmRequested
                                                    && !root.disarmUnconfirmed
                                                    && !mappingSettings.recoveryPending

        RowLayout {
            Layout.fillWidth: true
            spacing:         ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                elide:            Text.ElideRight
                text:             qsTr("已锁定：%1").arg(root.sessionVehicleText)
                color:            root.vehicleContextMatches ? root._qgcPal.text : root._qgcPal.warningText
            }

            QGCLabel {
                text:  root.vehicleReady ? qsTr("通信就绪") : qsTr("等待载具就绪")
                color: root.vehicleReady ? root._qgcPal.text : root._qgcPal.warningText
            }

            QGCLabel {
                visible: root.activeVehicle !== null
                text:    root.vehicleArmingUnconfirmed ? qsTr("上锁状态待确认")
                         : (root.activeVehicle && root.activeVehicle.armed ? qsTr("已解锁") : qsTr("已上锁"))
                color:   root.vehicleArmingUnconfirmed || (root.activeVehicle && root.activeVehicle.armed)
                         ? root._qgcPal.warningText : root._qgcPal.text
            }

            QGCButton {
                objectName: "outputSafeEnd"
                text:       root.disarmRequested ? qsTr("正在上锁") : qsTr("安全结束")
                enabled:    !root.disarmRequested
                                && (root.testEnabled || root.testSessionOwned || root.armingRequested
                                    || root.runningOutput !== -1 || root.directServoBusy() || root.vehicleArmingUnconfirmed)
                onClicked:  root.beginSafeShutdown(false, qsTr("用户结束测试"))
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            wrapMode:         Text.WordWrap
            color:            root._qgcPal.warningText
            text:             qsTr("接线记录保存接口与实际位置。快速修改飞控功能时，请先上锁并关闭测试，选择新功能后点击“保存”再次确认。测试前固定机器人并清空推进器周边。")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing:         ScreenTools.defaultFontPixelWidth
            visible:         toolTabs.currentIndex !== 0 || root.testEnabled || root.testSessionOwned
                                 || root.armingRequested || root.armCommandPending || root.disarmRequested
                                 || root.runningOutput !== -1 || root.directServoBusy()

            QGCCheckBox {
                id:         enableTestCheck
                text:       qsTr("启用测试")
                checked:    root.testEnabled
                enabled:    root.vehicleReady && !root.disarmRequested && !root.armingRequested
                                && !root.functionSaveConfirmation && (checked || root.testPathAvailable)
                onClicked: {
                    if (!root.vehicleReady) {
                        root.testEnabled = false
                        root.testStatusText = qsTr("载具未就绪，测试功能已停用。")
                        return
                    }
                    root.testEnabled = checked
                    if (checked) {
                        if (root.testMode === 0) {
                            root.testStatusText = qsTr("先等待冷却，再准备并点动一路电机。")
                            root.startCooldown(11)
                        } else {
                            root.testStatusText = qsTr("保持飞控上锁，可以测试一路物理输出。")
                        }
                    } else {
                        root.beginSafeShutdown(false, qsTr("测试已停用"))
                    }
                }
            }

            QGCLabel { text: qsTr("测试模式") }

            QGCComboBox {
                id:                     testModeCombo
                objectName:             "outputTestMode"
                Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 30
                model:                  [ qsTr("电机点动 (Motor Test)"), qsTr("高级：推进器 PWM 直测"), qsTr("舵机点动") ]
                currentIndex:           root.testMode
                // With the safety restrictions off only a test or a recovery that is running holds the mode.
                enabled:                root.activeVehicle && !root.activeVehicle.armed
                                            && (root.restrictionsOff
                                                ? (root.vehicleReady && !root.directServoBusy() && root.runningOutput === -1)
                                                : (toolLayout.settingsEditable && root.cooldownRemaining === 0))
                onActivated: (index) => {
                    root.testMode = index
                    toolTabs.currentIndex = index === 1 ? 2 : 1
                }
            }

            QGCLabel {
                visible: root.cooldownRemaining > 0
                text:    qsTr("冷却 %1 秒").arg(root.cooldownRemaining)
                color:   root._qgcPal.warningText
            }

            Item { Layout.fillWidth: true }

            QGCButton {
                objectName: "outputPrepareTest"
                visible:    root.testMode === 0 && toolTabs.currentIndex !== 0
                text:       root.armingRequested ? qsTr("正在解锁")
                                                : (root.activeVehicle && root.activeVehicle.armed ? qsTr("已准备") : qsTr("准备电机测试"))
                enabled:    root.canPrepareMotorTest
                onClicked:  root.prepareForNextTest()
            }
        }

        QGCLabel {
            Layout.fillWidth: true
            visible:          root.testStatusText.length > 0
            wrapMode:         Text.WordWrap
            text:             root.testStatusText
            color:            root.cooldownRemaining > 0 || root.disarmRequested ? root._qgcPal.warningText : root._qgcPal.text
        }

        QGCLabel {
            Layout.fillWidth: true
            visible:          root.directServoStatusText.length > 0
            wrapMode:         Text.WordWrap
            text:             root.directServoStatusText
            color:            root._qgcPal.warningText
        }

        QGCLabel {
            objectName: "outputMappingNotice"
            Layout.fillWidth: true
            visible:          root.outputMappingNotice.length > 0
            wrapMode:         Text.WordWrap
            text:             root.outputMappingNotice
            color:            root._qgcPal.warningText
        }

        QGCLabel {
            objectName: "outputFunctionSaveStatus"
            Layout.fillWidth: true
            visible: root.functionSaveStatusText.length > 0
            wrapMode: Text.WordWrap
            text: root.functionSaveStatusText
        }

        RowLayout {
            Layout.fillWidth: true
            visible:          mappingSettings.recoveryPending
            spacing:          ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
                text:             qsTr("待恢复：载具 %1，SERVO%2，原值 %3，记录于 %4")
                                          .arg(mappingSettings.recoveryVehicleUid.length > 0
                                                   ? mappingSettings.recoveryVehicleUid : mappingSettings.recoveryVehicleId)
                                          .arg(mappingSettings.recoveryOutput)
                                          .arg(mappingSettings.recoveryFunction)
                                          .arg(mappingSettings.recoveryTimestamp)
                color:            root._qgcPal.warningText
            }

            QGCButton {
                objectName: "outputRecovery"
                text:       root.recoveryOperatorConfirmation ? qsTr("确认设备已停止并恢复") : qsTr("恢复异常通道")
                enabled:    root.vehicleReady && root.recoveryMatchesSession() && root.recoveryPermitted
                                && root.activeVehicle && !root.activeVehicle.armed && !root.directServoBusy()
                                && !root.disarmRequested && !root.disarmUnconfirmed && !root.armingRequested
                onClicked:  root.recoverPendingServoFunction()
            }

            QGCButton {
                objectName: "outputRecoveryForce"
                visible:    root.restrictionsOff
                text:       root.recoveryForceConfirmation ? qsTr("确认设备已停止，直接恢复") : qsTr("人工确认完成")
                enabled:    root.vehicleReady && root.activeVehicle && !root.activeVehicle.armed && !root.directServoBusy()
                onClicked:  root.forceCompleteRecovery()
            }

            QGCButton {
                objectName: "outputRecoveryManualConfirm"
                visible:    root.recoveryManualConfirmAvailable
                text:       root.recoveryManualConfirmation ? qsTr("确认是这台机器人") : qsTr("没有序列号，人工确认")
                onClicked:  root.confirmRecoveryVehicleManually()
            }

            QGCButton {
                text:       root.recoveryDiscardConfirmation ? qsTr("确认忽略记录") : qsTr("忽略旧记录")
                enabled:    !root.directServoBusy() && root.recoveryJournalCanBeDiscarded()
                onClicked:  root.discardRecoveryJournal()
            }
        }

        QGCTabBar {
            id:                 toolTabs
            objectName:         "outputToolTabs"
            Layout.fillWidth:   true

            QGCTabButton { text: qsTr("接线记录") }
            QGCTabButton { text: qsTr("单路测试") }
            QGCTabButton { text: qsTr("高级输出测试") }
        }

        RowLayout {
            Layout.fillWidth: true
            visible:          toolTabs.currentIndex === 0
            spacing:          ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
                text:             root.mappingScopeText
                color:            root.recordVehicleUid.length > 0 ? root._qgcPal.text : root._qgcPal.warningText
            }

            QGCButton {
                visible:   root.legacyMappingsAvailable
                text:      root.legacyImportConfirmation ? qsTr("确认导入旧记录") : qsTr("导入旧记录")
                enabled:   root.canRecordMappings
                onClicked: root.importLegacyMappings()
            }

            QGCButton {
                text:      root.clearMappingsConfirmation ? qsTr("确认清空记录") : qsTr("清空当前记录")
                enabled:   root.canRecordMappings
                onClicked: root.clearMappings()
            }

            QGCButton {
                text:      qsTr("导出 CSV")
                onClicked: exportFileDialog.openForSave()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible:          toolTabs.currentIndex !== 0
            spacing:          ScreenTools.defaultFontPixelWidth

            QGCLabel {
                visible: root.testMode === 0
                text:    qsTr("功率")
            }

            SpinBox {
                visible:         root.testMode === 0
                from:            1
                to:              15
                value:           mappingSettings.testPercent
                editable:        true
                enabled:         toolLayout.settingsEditable
                onValueModified: mappingSettings.testPercent = value
            }

            QGCLabel {
                visible: root.testMode === 0
                text:    qsTr("%")
            }

            QGCLabel {
                visible: root.testMode !== 0
                text:    qsTr("目标 PWM")
            }

            SpinBox {
                visible: root.testMode !== 0
                from:    root.directServoMode ? 1000 : 800
                to:      2200
                value:   root.directServoMode ? mappingSettings.directServoPwm : mappingSettings.servoJogPwm
                editable: true
                enabled:  toolLayout.settingsEditable
                onValueModified: {
                    if (root.directServoMode) {
                        mappingSettings.directServoPwm = value
                    } else {
                        mappingSettings.servoJogPwm = value
                    }
                }
            }

            QGCLabel { text: qsTr("时长") }

            SpinBox {
                from:     1
                to:       5
                value:    root.testMode === 0 ? mappingSettings.testSeconds
                                             : (root.directServoMode ? mappingSettings.directServoSeconds : mappingSettings.servoJogSeconds)
                editable: true
                enabled:  toolLayout.settingsEditable
                onValueModified: {
                    if (root.testMode === 0) {
                        mappingSettings.testSeconds = value
                    } else if (root.directServoMode) {
                        mappingSettings.directServoSeconds = value
                    } else {
                        mappingSettings.servoJogSeconds = value
                    }
                }
            }

            QGCLabel { text: qsTr("秒") }

            QGCLabel {
                Layout.fillWidth: true
                text:             root.testMode === 0 ? qsTr("按飞控电机测试路径点动")
                                                      : qsTr("状态：%1；测试期间保持上锁").arg(root.directServoStateText())
                color:            root.testMode !== 0 && root.directServoState !== "idle" ? root._qgcPal.warningText : root._qgcPal.text
                elide:            Text.ElideRight
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible:          toolTabs.currentIndex === 2 && root.testMode === 0
            spacing:          ScreenTools.defaultFontPixelWidth

            QGCLabel { text: qsTr("Motor Test 发送方式") }

            QGCRadioButton {
                text:      qsTr("按测试编号")
                checked:   !mappingSettings.useServoFunctionForMotorTest
                enabled:   toolLayout.settingsEditable && root.cooldownRemaining === 0
                onClicked: mappingSettings.useServoFunctionForMotorTest = false
            }

            QGCRadioButton {
                text:      qsTr("按 SERVO_FUNCTION 映射")
                checked:   mappingSettings.useServoFunctionForMotorTest
                enabled:   toolLayout.settingsEditable && root.cooldownRemaining === 0
                onClicked: mappingSettings.useServoFunctionForMotorTest = true
            }

            Item { Layout.fillWidth: true }
        }

        QGCLabel {
            Layout.fillWidth: true
            visible:          toolTabs.currentIndex !== 0
            wrapMode:         Text.WordWrap
            color:            root._qgcPal.warningText
            text:             root.testMode === 0
                                  ? (mappingSettings.useServoFunctionForMotorTest
                                         ? qsTr("读取每行 SERVOx_FUNCTION 中的 MotorN，并发送 Motor Test N；测试编号与物理输出口可能不同。")
                                         : qsTr("第 N 行发送 Motor Test N。该编号由飞控定义，不一定等于物理 SERVO N；发送方式可在高级输出测试中切换。"))
                                  : (root.directServoMode
                                         ? (toolTabs.currentIndex === 2
                                                ? qsTr("PWM 直测按物理 SERVO 口发送 1000–2200 PWM，临时禁用该通道功能；结束回到 1500，并确认恢复原功能。")
                                                : qsTr("推进器 PWM 直测只在“高级输出测试”页执行，请切换页签；日常测试可选择电机点动或舵机点动。"))
                                         : qsTr("舵机点动按物理 SERVO 口发送 800–2200 PWM，临时禁用该通道功能；结束回到 SERVOx_TRIM，并确认恢复原功能。"))
        }

        QGCLabel {
            Layout.fillWidth: true
            visible:          toolTabs.currentIndex !== 0 && root.testMode !== 0
            wrapMode:         Text.WordWrap
            color:            root._qgcPal.warningText
            text:             qsTr("直接输出的时长由地面站计时；通信中断时可能无法回中，请准备独立的断电停止手段。")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing:         ScreenTools.defaultFontPixelWidth

            QGCLabel {
                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 12
                text:                  qsTr("物理输出口")
                font.bold:             true
            }

            QGCLabel {
                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 32
                text:                  qsTr("功能参数 / 快速修改")
                font.bold:             true
            }

            QGCLabel {
                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                visible:               toolTabs.currentIndex !== 0
                text:                  qsTr("当前模式测试")
                font.bold:             true
            }

            QGCLabel {
                Layout.fillWidth:      true
                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 24
                text:                  qsTr("主板接口 / 线束")
                font.bold:             true
            }

            QGCLabel {
                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 25
                text:                  qsTr("实际位置")
                font.bold:             true
            }
        }

        QGCFlickable {
            id:                 outputList
            Layout.fillWidth:   true
            Layout.fillHeight:  true
            Layout.minimumHeight: ScreenTools.defaultFontPixelHeight * 3
            indicatorColor:     root._qgcPal.text
            contentWidth:       width
            contentHeight:      outputRows.implicitHeight
            flickableDirection: Flickable.VerticalFlick

            ColumnLayout {
                id:         outputRows
                width:      outputList.width
                spacing:    ScreenTools.defaultFontPixelHeight * 0.3

                Repeater {
                    model: 16

                    RowLayout {
                        id:                 rowItem
                        required property int index
                        readonly property int outputNumber: index + 1
                        readonly property int motorTestTarget: mappingSettings.useServoFunctionForMotorTest
                                                                   ? root.motorNumberFromServoFunction(outputNumber) : outputNumber
                        readonly property var functionFact: root.servoFunctionFact(outputNumber)
                        property var stagedFunction: undefined
                        property bool functionUnconfirmed: false
                        Layout.fillWidth:   true
                        spacing:            ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 12
                            text:                  qsTr("SERVO%1").arg(rowItem.outputNumber)
                        }

                        RowLayout {
                            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 32
                            spacing: ScreenTools.defaultFontPixelWidth * 0.5

                            QGCLabel {
                                objectName: "outputFunction" + rowItem.outputNumber
                                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 10
                                text: root.servoFunctionText(rowItem.outputNumber)
                                          + (rowItem.functionUnconfirmed ? qsTr("（未确认）") : "")
                                color: rowItem.functionUnconfirmed || text === qsTr("Disabled")
                                           || text === qsTr("参数缺失") || text === qsTr("参数不可用")
                                           ? root._qgcPal.warningText : root._qgcPal.text
                                elide: Text.ElideRight
                            }

                            QGCComboBox {
                                id: functionChoice
                                objectName: "outputFunctionChoice" + rowItem.outputNumber
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                                sizeToContents: true
                                model: rowItem.functionFact ? rowItem.functionFact.enumStrings : []
                                currentIndex: root.functionEnumIndex(rowItem.functionFact, rowItem.stagedFunction)
                                alternateText: rowItem.stagedFunction === undefined ? qsTr("选择新功能") : ""
                                enabled: root.canConfigureServoFunctions && !root.functionSaveConfirmation
                                             && rowItem.functionFact && !rowItem.functionFact.readOnly && count > 0
                                onActivated: (index) => {
                                    if (enabled && index >= 0 && index < rowItem.functionFact.enumValues.length) {
                                        rowItem.stagedFunction = Number(rowItem.functionFact.enumValues[index])
                                    }
                                }
                            }

                            QGCButton {
                                objectName: "outputFunctionSave" + rowItem.outputNumber
                                Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 6
                                text: qsTr("保存")
                                enabled: functionChoice.enabled && rowItem.stagedFunction !== undefined
                                onClicked: root.requestServoFunctionSave(rowItem.outputNumber, rowItem.stagedFunction)
                            }

                            Connections {
                                target: directControl
                                function onFunctionSaveFinished(output, success, message) {
                                    if (output === rowItem.outputNumber) {
                                        rowItem.functionUnconfirmed = !success
                                        if (success) {
                                            rowItem.stagedFunction = undefined
                                        }
                                    }
                                }
                            }
                        }

                        QGCButton {
                            objectName:            "outputTestAction" + rowItem.outputNumber
                            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 15
                            visible:               toolTabs.currentIndex !== 0
                            text:                  root.testMode === 0
                                                       ? (root.runningOutput === rowItem.outputNumber ? qsTr("测试中")
                                                                                                       : (root.cooldownRemaining > 0 ? qsTr("冷却")
                                                                                                                                    : (rowItem.motorTestTarget > 0 ? qsTr("点动 Motor%1").arg(rowItem.motorTestTarget)
                                                                                                                                                                  : qsTr("无电机映射"))))
                                                       : (root.directServoOutput === rowItem.outputNumber || root.directServoPendingOutput === rowItem.outputNumber
                                                              ? qsTr("测试中") : (root.directServoMode ? qsTr("直发 PWM") : qsTr("点动舵机")))
                            enabled:               root.testMode === 0 ? root.canStartMotorTest && rowItem.motorTestTarget > 0
                                                                      : (root.canStartDirectTest && (!root.directServoMode || toolTabs.currentIndex === 2))
                            onClicked: {
                                if (root.testMode === 0) {
                                    root.testOutput(rowItem.outputNumber)
                                } else if (root.directServoMode) {
                                    root.startDirectServoTest(rowItem.outputNumber)
                                } else {
                                    root.startServoJogTest(rowItem.outputNumber)
                                }
                            }
                        }

                        QGCTextField {
                            id:                    portField
                            objectName:            "outputRecordPort" + rowItem.outputNumber
                            Layout.fillWidth:      true
                            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 24
                            placeholderText:       qsTr("如 MAIN1 / J3-1")
                            text:                  root.portName(rowItem.outputNumber)
                            enabled:               root.canRecordMappings
                            onEditingFinished:     root.setPortName(rowItem.outputNumber, text)

                            Connections {
                                target: root
                                function onMappingsUpdated() {
                                    portField.text = root.portName(rowItem.outputNumber)
                                }
                            }
                        }

                        QGCComboBox {
                            id:                    positionCombo
                            objectName:            "outputRecordPosition" + rowItem.outputNumber
                            Layout.preferredWidth: ScreenTools.defaultFontPixelWidth * 25
                            model:                 root.positionNames
                            currentIndex:          root.positionIndex(rowItem.outputNumber)
                            enabled:               root.canRecordMappings
                            onActivated: (index) => root.setPositionIndex(rowItem.outputNumber, index)

                            Connections {
                                target: root
                                function onMappingsUpdated() {
                                    positionCombo.currentIndex = root.positionIndex(rowItem.outputNumber)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

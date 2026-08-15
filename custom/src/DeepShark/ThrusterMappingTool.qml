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
import QGroundControl.Controllers
import QGroundControl.ScreenTools
import DeepShark

QGCPopupDialog {
    id:             root
    title:          qsTr("SERVO输出扫描向导")
    buttons:        Dialog.Close

    readonly property var currentActiveVehicle: QGroundControl.multiVehicleManager.activeVehicle
    readonly property var activeVehicle:         paramController.vehicle
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
    property bool   directServoMode:    false
    property bool   servoJogMode:       false
    property string directServoOperation: "thruster"
    property int    directServoOutput:  -1
    property int    directServoPendingOutput: -1
    property int    directServoOriginalFunction: -1
    property int    directServoTestPwm: 1500
    property int    directServoTestSeconds: 1
    property int    directServoNeutralPwm: 1500
    property var    directServoFact:    null
    property string directServoState:   "idle"
    property int    directServoWaitTicks: 0
    property string directServoFailureReason: ""
    property bool   recoveryDiscardConfirmation: false
    property var    recoveryVehicleObject: null
    property bool   disarmRequested:     false
    property bool   closeAfterSafeShutdown: false
    property int    cooldownRemaining:  0
    property bool   armingRequested:    false
    property string testStatusText:     ""
    property string directServoStatusText: ""
    readonly property int motorTestCommand: 209
    readonly property int setServoCommand: 183
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

    ParameterEditorController {
        id: paramController
    }

    ThrusterMappingExportController {
        id: exportController
    }

    Settings {
        id:         mappingSettings
        category:   "DeepSharkServoOutputMapping"

        property int    testPercent:    5
        property int    testSeconds:    1
        property bool   useServoFunctionForMotorTest: false
        property int    directServoPwm: 1550
        property int    directServoSeconds: 1
        property int    servoJogPwm:    1200
        property int    servoJogSeconds: 1
        property bool   hasFunctionBackup: false

        // Legacy backup values remain readable for compatibility, but are never
        // automatically restored. Recovery below is scoped to one vehicle/output.
        property bool   recoveryPending: false
        property int    recoveryVehicleId: -1
        property string recoveryVehicleUid: ""
        property int    recoveryOutput: -1
        property int    recoveryFunction: -1
        property string recoveryTimestamp: ""

        property int    backupServo1Function: 0
        property int    backupServo2Function: 0
        property int    backupServo3Function: 0
        property int    backupServo4Function: 0
        property int    backupServo5Function: 0
        property int    backupServo6Function: 0
        property int    backupServo7Function: 0
        property int    backupServo8Function: 0
        property int    backupServo9Function: 0
        property int    backupServo10Function: 0
        property int    backupServo11Function: 0
        property int    backupServo12Function: 0
        property int    backupServo13Function: 0
        property int    backupServo14Function: 0
        property int    backupServo15Function: 0
        property int    backupServo16Function: 0

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

    onRejected: {
        if (root.directServoBusy()
                || root.disarmRequested
                || root.runningOutput !== -1
                || root.testEnabled) {
            preventClose = true
            root.beginSafeShutdown(true, qsTr("用户请求关闭向导"))
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

        root.beginSafeShutdown(false, qsTr("活动载具已切换"))
        if (!root.directServoBusy() && root.activeVehicle) {
            root.directServoStatusText = qsTr("活动载具与向导锁定载具不一致，测试功能已停用。")
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

    function servoFunctionText(outputNumber) {
        var paramName = servoParamName(outputNumber)
        if (!paramController.parameterExists(-1, paramName)) {
            return qsTr("参数缺失")
        }

        var fact = paramController.getParameterFact(-1, paramName, false)
        var text = fact.enumOrValueString
        return text && text.length > 0 ? text : fact.rawValue.toString()
    }

    function servoFunctionValue(outputNumber) {
        var paramName = servoParamName(outputNumber)
        if (!paramController.parameterExists(-1, paramName)) {
            return null
        }

        return paramController.getParameterFact(-1, paramName, false).rawValue
    }

    function setServoFunctionValue(outputNumber, value) {
        var paramName = servoParamName(outputNumber)
        if (!paramController.parameterExists(-1, paramName)) {
            return false
        }

        paramController.getParameterFact(-1, paramName, false).rawValue = value
        return true
    }

    function motorNumberFromServoFunction(outputNumber) {
        var rawValue = Number(root.servoFunctionValue(outputNumber))
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

    function portName(outputNumber) {
        switch (outputNumber) {
        case 1: return mappingSettings.port1Name
        case 2: return mappingSettings.port2Name
        case 3: return mappingSettings.port3Name
        case 4: return mappingSettings.port4Name
        case 5: return mappingSettings.port5Name
        case 6: return mappingSettings.port6Name
        case 7: return mappingSettings.port7Name
        case 8: return mappingSettings.port8Name
        case 9: return mappingSettings.port9Name
        case 10: return mappingSettings.port10Name
        case 11: return mappingSettings.port11Name
        case 12: return mappingSettings.port12Name
        case 13: return mappingSettings.port13Name
        case 14: return mappingSettings.port14Name
        case 15: return mappingSettings.port15Name
        case 16: return mappingSettings.port16Name
        default: return ""
        }
    }

    function setPortName(outputNumber, name) {
        switch (outputNumber) {
        case 1: mappingSettings.port1Name = name; break
        case 2: mappingSettings.port2Name = name; break
        case 3: mappingSettings.port3Name = name; break
        case 4: mappingSettings.port4Name = name; break
        case 5: mappingSettings.port5Name = name; break
        case 6: mappingSettings.port6Name = name; break
        case 7: mappingSettings.port7Name = name; break
        case 8: mappingSettings.port8Name = name; break
        case 9: mappingSettings.port9Name = name; break
        case 10: mappingSettings.port10Name = name; break
        case 11: mappingSettings.port11Name = name; break
        case 12: mappingSettings.port12Name = name; break
        case 13: mappingSettings.port13Name = name; break
        case 14: mappingSettings.port14Name = name; break
        case 15: mappingSettings.port15Name = name; break
        case 16: mappingSettings.port16Name = name; break
        }
        mappingsUpdated()
    }

    function positionIndex(outputNumber) {
        switch (outputNumber) {
        case 1: return mappingSettings.servo1Position
        case 2: return mappingSettings.servo2Position
        case 3: return mappingSettings.servo3Position
        case 4: return mappingSettings.servo4Position
        case 5: return mappingSettings.servo5Position
        case 6: return mappingSettings.servo6Position
        case 7: return mappingSettings.servo7Position
        case 8: return mappingSettings.servo8Position
        case 9: return mappingSettings.servo9Position
        case 10: return mappingSettings.servo10Position
        case 11: return mappingSettings.servo11Position
        case 12: return mappingSettings.servo12Position
        case 13: return mappingSettings.servo13Position
        case 14: return mappingSettings.servo14Position
        case 15: return mappingSettings.servo15Position
        case 16: return mappingSettings.servo16Position
        default: return 0
        }
    }

    function setPositionIndex(outputNumber, index) {
        switch (outputNumber) {
        case 1: mappingSettings.servo1Position = index; break
        case 2: mappingSettings.servo2Position = index; break
        case 3: mappingSettings.servo3Position = index; break
        case 4: mappingSettings.servo4Position = index; break
        case 5: mappingSettings.servo5Position = index; break
        case 6: mappingSettings.servo6Position = index; break
        case 7: mappingSettings.servo7Position = index; break
        case 8: mappingSettings.servo8Position = index; break
        case 9: mappingSettings.servo9Position = index; break
        case 10: mappingSettings.servo10Position = index; break
        case 11: mappingSettings.servo11Position = index; break
        case 12: mappingSettings.servo12Position = index; break
        case 13: mappingSettings.servo13Position = index; break
        case 14: mappingSettings.servo14Position = index; break
        case 15: mappingSettings.servo15Position = index; break
        case 16: mappingSettings.servo16Position = index; break
        }
        mappingsUpdated()
    }

    function clearMappings() {
        for (var output = 1; output <= 16; output++) {
            setPortName(output, "")
            setPositionIndex(output, 0)
        }
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
        lines.push(root.csvLine(["DeepShark Thruster Mapping Export"]))
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

        return false
    }

    function saveRecoveryJournal(outputNumber, originalFunction) {
        mappingSettings.recoveryVehicleId = root.activeVehicle.id
        mappingSettings.recoveryVehicleUid = Number(root.activeVehicle.vehicleUID) > 0
                ? root.activeVehicle.vehicleUIDStr
                : ""
        mappingSettings.recoveryOutput = outputNumber
        mappingSettings.recoveryFunction = originalFunction
        mappingSettings.recoveryTimestamp = new Date().toLocaleString()
        mappingSettings.recoveryPending = true
        root.recoveryVehicleObject = root.activeVehicle
    }

    function clearRecoveryJournal() {
        discardRecoveryConfirmTimer.stop()
        root.recoveryDiscardConfirmation = false
        mappingSettings.recoveryPending = false
        mappingSettings.recoveryVehicleId = -1
        mappingSettings.recoveryVehicleUid = ""
        mappingSettings.recoveryOutput = -1
        mappingSettings.recoveryFunction = -1
        mappingSettings.recoveryTimestamp = ""
        root.recoveryVehicleObject = null
    }

    function directServoBusy() {
        return root.directServoState !== "idle" && root.directServoState !== "recoveryNeeded"
    }

    function directServoStateText() {
        switch (root.directServoState) {
        case "waitingDisable": return qsTr("等待禁用确认")
        case "disableSettling": return qsTr("准备输出")
        case "waitingTestAck": return qsTr("等待测试回执")
        case "testing": return qsTr("正在输出")
        case "waitingNeutralAck": return qsTr("等待回中回执")
        case "waitingRestore": return qsTr("正在恢复参数")
        case "recoveryNeeded": return qsTr("需要人工恢复")
        default: return qsTr("空闲")
        }
    }

    function directParameterWriteConfirmed(expectedValue) {
        return root.directServoFact !== null
                && Number(root.directServoFact.rawValue) === expectedValue
                && root.activeVehicle !== null
                && !root.activeVehicle.parameterManager.pendingWrites
    }

    function recoveryJournalCanBeDiscarded() {
        if (!mappingSettings.recoveryPending || !root.recoveryMatchesSession()) {
            return true
        }

        var fact = paramController.getParameterFact(-1, root.servoParamName(mappingSettings.recoveryOutput), false)
        return fact !== null && Number(fact.rawValue) !== 0
    }

    function directServoOperationText() {
        return root.directServoOperation === "servo" ? qsTr("舵机点动") : qsTr("推进器 PWM 直测")
    }

    function startDirectServoTest(outputNumber) {
        root.directServoMode = true
        root.startPhysicalServoTest(outputNumber, false)
    }

    function startServoJogTest(outputNumber) {
        root.servoJogMode = true
        root.startPhysicalServoTest(outputNumber, true)
    }

    function startPhysicalServoTest(outputNumber, servoJog) {
        root.directServoOperation = servoJog ? "servo" : "thruster"
        root.directServoTestPwm = servoJog ? mappingSettings.servoJogPwm : mappingSettings.directServoPwm
        root.directServoTestSeconds = servoJog ? mappingSettings.servoJogSeconds : mappingSettings.directServoSeconds
        root.directServoNeutralPwm = 1500

        if (root.disarmRequested) {
            root.directServoStatusText = qsTr("正在等待飞控确认上锁，暂不能开始%1。").arg(root.directServoOperationText())
            return
        }

        if (!root.vehicleContextMatches) {
            root.directServoStatusText = qsTr("当前活动载具与向导锁定载具不一致。请关闭并重新打开向导。")
            return
        }

        if (root.directServoBusy()) {
            root.directServoStatusText = qsTr("上一项直接输出仍在处理，请等待恢复完成。")
            return
        }

        if (root.runningOutput !== -1) {
            root.directServoStatusText = qsTr("Motor Test 正在执行中，暂不能直接测试 SERVO。")
            return
        }

        if (root.activeVehicle.armed) {
            root.directServoStatusText = qsTr("直接 SERVO 输出要求飞控上锁。请先上锁再测试。")
            return
        }

        if (root.activeVehicle.parameterManager.pendingWrites) {
            root.directServoStatusText = qsTr("飞控仍有参数写入任务，请等待完成后再开始直接输出。")
            return
        }

        if (mappingSettings.recoveryPending) {
            root.directServoStatusText = root.recoveryMatchesSession()
                    ? qsTr("存在未确认恢复的 SERVO%1，请先处理恢复记录。").arg(mappingSettings.recoveryOutput)
                    : qsTr("存在另一台载具的未完成恢复记录，请先连接对应载具处理，或明确忽略该记录。")
            return
        }

        var fact = paramController.getParameterFact(-1, root.servoParamName(outputNumber), false)
        if (!fact) {
            root.directServoStatusText = qsTr("缺少 %1，无法开始%2。")
                    .arg(root.servoParamName(outputNumber))
                    .arg(root.directServoOperationText())
            return
        }

        if (servoJog) {
            var trimParamName = root.servoTrimParamName(outputNumber)
            var trimFact = paramController.getParameterFact(-1, trimParamName, false)
            var trimPwm = trimFact ? Number(trimFact.rawValue) : NaN
            if (isNaN(trimPwm) || trimPwm < 800 || trimPwm > 2200) {
                root.directServoStatusText = qsTr("%1 缺失或无效，无法保证点动后安全回中。").arg(trimParamName)
                return
            }

            var minParamName = root.servoLimitParamName(outputNumber, "MIN")
            var maxParamName = root.servoLimitParamName(outputNumber, "MAX")
            var minFact = paramController.getParameterFact(-1, minParamName, false)
            var maxFact = paramController.getParameterFact(-1, maxParamName, false)
            var minPwm = minFact ? Number(minFact.rawValue) : NaN
            var maxPwm = maxFact ? Number(maxFact.rawValue) : NaN
            if (isNaN(minPwm) || isNaN(maxPwm) || minPwm > maxPwm) {
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
        if (isNaN(originalFunction)) {
            root.directServoStatusText = qsTr("%1 当前值无效，无法安全备份。").arg(root.servoParamName(outputNumber))
            return
        }

        root.directServoFact = fact
        root.directServoOriginalFunction = originalFunction
        root.directServoPendingOutput = outputNumber
        root.directServoFailureReason = ""
        root.directServoState = "waitingDisable"
        root.directServoWaitTicks = 0
        root.saveRecoveryJournal(outputNumber, originalFunction)
        root.directServoStatusText = qsTr("%1：已备份 %2=%3，正在等待飞控确认 Disabled。")
                .arg(root.directServoOperationText())
                .arg(root.servoParamName(outputNumber))
                .arg(originalFunction)
        root.directServoFact.rawValue = 0
        directParameterMonitor.restart()
    }

    function stopDirectServoTimers() {
        if (directServoSettleTimer.running) {
            directServoSettleTimer.stop()
        }
        if (directServoStopTimer.running) {
            directServoStopTimer.stop()
        }
        if (directCommandTimeout.running) {
            directCommandTimeout.stop()
        }
        if (directParameterMonitor.running) {
            directParameterMonitor.stop()
        }
    }

    function sendDirectServoPwm(outputNumber, pwm) {
        if (!root.activeVehicle) {
            return
        }

        root.activeVehicle.sendCommand(root.autopilotComponentId, root.setServoCommand, false, outputNumber, pwm)
    }

    function beginDirectServoOutput() {
        if (!root.activeVehicle || root.directServoPendingOutput === -1 || root.directServoState !== "disableSettling") {
            root.beginDirectServoRestore(qsTr("载具连接或直测状态发生变化"))
            return
        }

        root.directServoOutput = root.directServoPendingOutput
        root.directServoPendingOutput = -1
        root.directServoState = "waitingTestAck"
        root.sendDirectServoPwm(root.directServoOutput, root.directServoTestPwm)
        root.directServoStatusText = qsTr("已发送 SERVO%1=%2 PWM，等待飞控确认。")
                .arg(root.directServoOutput)
                .arg(root.directServoTestPwm)
        directCommandTimeout.restart()
    }

    function beginDirectServoNeutral() {
        if (root.directServoOutput === -1) {
            root.beginDirectServoRestore(qsTr("测试通道状态无效"))
            return
        }

        directServoStopTimer.stop()
        directCommandTimeout.stop()
        root.directServoState = "waitingNeutralAck"
        root.sendDirectServoPwm(root.directServoOutput, root.directServoNeutralPwm)
        root.directServoStatusText = qsTr("正在将 SERVO%1 回中到 TRIM=%2，等待飞控确认。")
                .arg(root.directServoOutput)
                .arg(root.directServoNeutralPwm)
        directCommandTimeout.restart()
    }

    function beginDirectServoRestore(reason) {
        directServoSettleTimer.stop()
        directServoStopTimer.stop()
        directCommandTimeout.stop()

        if (reason && reason.length > 0) {
            root.directServoFailureReason = reason
        }

        root.directServoOutput = -1
        root.directServoPendingOutput = -1

        if (!root.directServoFact || root.directServoOriginalFunction < 0) {
            root.directServoState = "recoveryNeeded"
            root.directServoStatusText = qsTr("无法自动恢复：%1。恢复记录已保留。")
                    .arg(root.directServoFailureReason.length > 0 ? root.directServoFailureReason : qsTr("参数对象不可用"))
            return
        }

        root.directServoState = "waitingRestore"
        root.directServoWaitTicks = 0
        root.directServoFact.rawValue = root.directServoOriginalFunction
        root.directServoStatusText = qsTr("正在恢复 %1=%2。")
                .arg(root.directServoFact.name)
                .arg(root.directServoOriginalFunction)
        directParameterMonitor.restart()
    }

    function completeDirectServoRestore() {
        var outputNumber = mappingSettings.recoveryOutput
        var warning = root.directServoFailureReason
        root.stopDirectServoTimers()
        root.clearRecoveryJournal()
        root.directServoState = "idle"
        root.directServoOutput = -1
        root.directServoPendingOutput = -1
        root.directServoOriginalFunction = -1
        root.directServoNeutralPwm = 1500
        root.directServoFact = null
        root.directServoFailureReason = ""
        root.directServoStatusText = warning.length > 0
                ? qsTr("SERVO%1 已确认恢复；本次%2提前结束：%3。").arg(outputNumber).arg(root.directServoOperationText()).arg(warning)
                : qsTr("SERVO%1 已回中，并确认恢复原功能参数。").arg(outputNumber)
        root.requestSafeDisarm(root.closeAfterSafeShutdown, qsTr("%1已结束").arg(root.directServoOperationText()))
    }

    function markDirectServoRecoveryNeeded(reason) {
        root.stopDirectServoTimers()
        root.closeAfterSafeShutdown = false
        root.directServoState = "recoveryNeeded"
        root.directServoOutput = -1
        root.directServoPendingOutput = -1
        root.directServoStatusText = qsTr("恢复尚未确认：%1。请保持飞控上锁并使用“恢复异常通道”。").arg(reason)
    }

    function recoverPendingServoFunction() {
        if (!mappingSettings.recoveryPending) {
            root.directServoStatusText = qsTr("没有待恢复记录。")
            return
        }

        if (!root.vehicleContextMatches || !root.recoveryMatchesSession()) {
            root.directServoStatusText = qsTr("恢复记录与当前载具不匹配，已拒绝写入。")
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

        var fact = paramController.getParameterFact(-1, root.servoParamName(mappingSettings.recoveryOutput), false)
        if (!fact) {
            root.directServoStatusText = qsTr("缺少恢复记录对应的参数，无法恢复。")
            return
        }

        var currentFunction = Number(fact.rawValue)
        if (currentFunction === mappingSettings.recoveryFunction) {
            var restoredOutput = mappingSettings.recoveryOutput
            root.clearRecoveryJournal()
            root.directServoState = "idle"
            root.directServoStatusText = qsTr("SERVO%1 已经是原值，恢复记录已清除。").arg(restoredOutput)
            return
        }

        if (currentFunction !== 0) {
            root.directServoStatusText = qsTr("SERVO%1 当前值为 %2，不是 Disabled，也不是记录原值；为避免覆盖新配置，已拒绝恢复。")
                    .arg(mappingSettings.recoveryOutput)
                    .arg(currentFunction)
            return
        }

        root.directServoFact = fact
        root.directServoOriginalFunction = mappingSettings.recoveryFunction
        root.directServoFailureReason = qsTr("人工恢复")
        root.directServoState = "waitingRestore"
        root.directServoWaitTicks = 0
        root.directServoFact.rawValue = root.directServoOriginalFunction
        root.directServoStatusText = qsTr("正在恢复异常通道 SERVO%1。").arg(mappingSettings.recoveryOutput)
        directParameterMonitor.restart()
    }

    function discardRecoveryJournal() {
        if (!mappingSettings.recoveryPending || root.directServoBusy()) {
            return
        }

        if (!root.recoveryJournalCanBeDiscarded()) {
            root.directServoStatusText = qsTr("当前匹配载具的异常通道仍为 Disabled，不能忽略记录；请先执行恢复。")
            return
        }

        if (!root.recoveryDiscardConfirmation) {
            root.recoveryDiscardConfirmation = true
            root.directServoStatusText = qsTr("再次点击“确认忽略记录”才会清除本机恢复记录；不会写入飞控。")
            discardRecoveryConfirmTimer.restart()
            return
        }

        root.clearRecoveryJournal()
        root.directServoState = "idle"
        root.directServoFact = null
        root.directServoOriginalFunction = -1
        root.directServoStatusText = qsTr("已忽略本机旧恢复记录；未向飞控写入任何参数。")
    }

    function abortDirectServoTest(reason) {
        if (!root.directServoBusy()) {
            return
        }

        root.directServoFailureReason = reason
        if (root.directServoState === "testing" || root.directServoState === "waitingTestAck") {
            root.beginDirectServoNeutral()
        } else if (root.directServoState !== "waitingNeutralAck" && root.directServoState !== "waitingRestore") {
            root.beginDirectServoRestore(reason)
        }
    }

    function finishDirectServoTest() {
        if (root.directServoState === "testing") {
            root.beginDirectServoNeutral()
        }
    }

    function handleDirectCommandResult(ackResult) {
        directCommandTimeout.stop()

        if (root.directServoState === "waitingTestAck") {
            if (ackResult === 0) {
                root.directServoState = "testing"
                root.directServoStatusText = qsTr("正在直接输出 SERVO%1 = %2 PWM。")
                        .arg(root.directServoOutput)
                        .arg(root.directServoTestPwm)
                directServoStopTimer.interval = Math.max(1, root.directServoTestSeconds) * 1000
                directServoStopTimer.restart()
            } else {
                root.beginDirectServoRestore(qsTr("飞控拒绝测试 PWM 指令"))
            }
            return
        }

        if (root.directServoState === "waitingNeutralAck") {
            root.beginDirectServoRestore(ackResult === 0 ? "" : qsTr("回中指令被拒绝"))
        }
    }

    function testOutput(outputNumber) {
        if (root.disarmRequested || !root.vehicleContextMatches || root.runningOutput !== -1 || root.cooldownRemaining > 0) {
            if (!root.vehicleContextMatches) {
                root.testStatusText = qsTr("当前活动载具与向导锁定载具不一致。请关闭并重新打开向导。")
            }
            return
        }

        if (!root.activeVehicle.armed) {
            root.testStatusText = qsTr("飞控当前已上锁。请先点击“准备下一次测试”，解锁并等待冷却结束。")
            return
        }

        var motorTarget = root.motorTestTargetForOutput(outputNumber)
        if (motorTarget < 1) {
            return
        }

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
        if (!root.activeVehicle || !root.vehicleContextMatches || root.runningOutput === -1) {
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

        root.requestSafeDisarm(root.closeAfterSafeShutdown, reason)
    }

    function requestSafeDisarm(closeAfter, reason) {
        root.closeAfterSafeShutdown = root.closeAfterSafeShutdown || closeAfter
        root.testEnabled = false
        root.armingRequested = false

        if (!root.activeVehicle) {
            root.disarmRequested = false
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("载具连接已丢失，无法确认上锁；向导保持打开。")
            return
        }

        if (!root.activeVehicle.armed) {
            root.completeSafeShutdown()
            return
        }

        if (root.disarmRequested) {
            return
        }

        root.disarmRequested = true
        root.testStatusText = qsTr("%1，正在请求飞控上锁。").arg(reason)
        safeDisarmTimeout.restart()
        root.activeVehicle.armed = false
    }

    function completeSafeShutdown() {
        safeDisarmTimeout.stop()
        root.disarmRequested = false
        root.armingRequested = false
        root.testEnabled = false
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
        if (root.disarmRequested) {
            root.testStatusText = qsTr("正在等待飞控确认上锁，请稍候。")
            return
        }

        if (!root.vehicleContextMatches) {
            root.testStatusText = qsTr("当前活动载具与向导锁定载具不一致，无法准备测试。请重新打开向导。")
            return
        }

        root.testEnabled = true
        if (motorTestPulseTimer.running) {
            motorTestPulseTimer.stop()
        }
        root.runningOutput = -1
        root.runningMotorTarget = -1
        root.motorTestPulsesRemaining = 0

        if (root.activeVehicle.armed) {
            root.armingRequested = false
            root.testStatusText = root.cooldownRemaining > 0 ?
                        qsTr("已解锁，请等待冷却倒计时结束后再点动。") :
                        qsTr("已准备，可以点动一路输出。")
            return
        }

        root.armingRequested = true
        root.testStatusText = qsTr("正在请求解锁。解锁成功后会等待冷却，再允许点动。")
        root.activeVehicle.armed = true
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
            if (root.activeVehicle && !root.activeVehicle.armed) {
                root.completeSafeShutdown()
                return
            }

            root.disarmRequested = false
            root.closeAfterSafeShutdown = false
            root.testStatusText = qsTr("未确认飞控上锁，向导保持打开。请在主界面手动上锁并确认后再关闭。")
        }
    }

    Timer {
        id:         directServoSettleTimer
        interval:   300
        repeat:     false
        onTriggered: root.beginDirectServoOutput()
    }

    Timer {
        id:         directServoStopTimer
        interval:   1000
        repeat:     false
        onTriggered: root.finishDirectServoTest()
    }

    Timer {
        id:         directCommandTimeout
        interval:   4500
        repeat:     false
        onTriggered: {
            if (root.directServoState === "waitingTestAck") {
                root.directServoFailureReason = qsTr("测试 PWM 指令无回执")
                root.beginDirectServoNeutral()
            } else if (root.directServoState === "waitingNeutralAck") {
                root.beginDirectServoRestore(qsTr("回中指令无回执"))
            }
        }
    }

    Timer {
        id:         discardRecoveryConfirmTimer
        interval:   5000
        repeat:     false
        onTriggered: root.recoveryDiscardConfirmation = false
    }

    Timer {
        id:         directParameterMonitor
        interval:   100
        repeat:     true
        onTriggered: {
            root.directServoWaitTicks++

            if (root.directServoState === "waitingDisable") {
                if (root.directParameterWriteConfirmed(0)) {
                    stop()
                    root.directServoState = "disableSettling"
                    root.directServoStatusText = qsTr("%1 已确认 Disabled，准备发送测试 PWM。")
                            .arg(root.directServoFact.name)
                    directServoSettleTimer.restart()
                } else if (root.directServoWaitTicks >= 80) {
                    stop()
                    root.beginDirectServoRestore(qsTr("等待 Disabled 写入确认超时"))
                }
                return
            }

            if (root.directServoState === "waitingRestore") {
                if (root.directParameterWriteConfirmed(root.directServoOriginalFunction)) {
                    stop()
                    root.completeDirectServoRestore()
                } else if (root.directServoWaitTicks >= 120) {
                    stop()
                    root.markDirectServoRecoveryNeeded(qsTr("等待原功能参数写回确认超时"))
                }
            }
        }
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
                root.runningOutput = -1
                root.runningMotorTarget = -1
                root.testStatusText = root.activeVehicle && !root.activeVehicle.armed ?
                            qsTr("冷却结束。飞控当前已上锁，点击“准备下一次测试”。") :
                            qsTr("可以继续测试下一路输出。")
                stop()
            }
        }
    }

    Connections {
        target: root.activeVehicle

        function onMavCommandResult(vehicleId, targetComponent, command, ackResult, failureCode) {
            if (!root.activeVehicle || vehicleId !== root.activeVehicle.id) {
                return
            }

            if (command === root.armDisarmCommand && (root.armingRequested || root.disarmRequested)) {
                if (ackResult !== 0) {
                    if (root.disarmRequested) {
                        safeDisarmTimeout.stop()
                        root.disarmRequested = false
                        root.closeAfterSafeShutdown = false
                        root.testStatusText = qsTr("飞控拒绝上锁，向导保持打开。请在主界面手动上锁并检查故障信息。")
                    } else {
                        root.armingRequested = false
                        root.testStatusText = qsTr("飞控拒绝解锁。请检查安全开关、模式和故障信息。")
                    }
                }
                return
            }

            if (command === root.setServoCommand) {
                if (targetComponent !== root.autopilotComponentId) {
                    return
                }
                if (root.directServoState === "waitingTestAck" || root.directServoState === "waitingNeutralAck") {
                    root.handleDirectCommandResult(ackResult)
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
                root.completeSafeShutdown()
                return
            }

            if (!root.vehicleContextMatches) {
                return
            }

            if (armed) {
                if (root.armingRequested) {
                    root.armingRequested = false
                    root.testEnabled = true
                    root.runningOutput = -1
                    root.runningMotorTarget = -1
                    root.startCooldown(11)
                    root.testStatusText = qsTr("已解锁。请等待冷却倒计时结束后再点动。")
                } else if (root.testEnabled && root.cooldownRemaining === 0) {
                    root.testStatusText = qsTr("已解锁，可以点动一路输出。")
                }
                return
            }

            root.armingRequested = false
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
            if (mappingSettings.recoveryPending
                    && root.recoveryVehicleObject === root.activeVehicle
                    && mappingSettings.recoveryVehicleUid.length === 0
                    && Number(root.activeVehicle.vehicleUID) > 0) {
                mappingSettings.recoveryVehicleUid = root.activeVehicle.vehicleUIDStr
            }
        }
    }

    QGCFileDialog {
        id:             exportFileDialog
        title:          qsTr("导出推进器映射")
        folder:         root.appSettings ? root.appSettings.parameterSavePath : ""
        nameFilters:    [ qsTr("CSV Files (*.csv)"), qsTr("All Files (*)") ]
        defaultSuffix:  "csv"

        onAcceptedForSave: (file) => {
            root.exportMappingCsv(file)
            close()
        }
    }

    ColumnLayout {
        width:      Math.min(mainWindow.width * 0.92, ScreenTools.defaultFontPixelWidth * 128)
        spacing:    ScreenTools.defaultFontPixelHeight * 0.55

        QGCLabel {
            Layout.fillWidth:   true
            elide:              Text.ElideRight
            color:              root._qgcPal.warningText
            text:               qsTr("逐路点动输出并记录接口/线束和推进器位置。功能列只读 SERVOx_FUNCTION；测试前请固定机器人并清空推进器周边。")
        }

        QGCLabel {
            Layout.fillWidth:   true
            elide:              Text.ElideRight
            text:               qsTr("本向导已锁定：%1").arg(root.sessionVehicleText)
            color:              root.vehicleContextMatches ? root._qgcPal.text : root._qgcPal.warningText
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCCheckBox {
                id:         enableTestCheck
                text:       qsTr("启用测试")
                checked:    root.testEnabled
                enabled:    root.vehicleContextMatches && !root.disarmRequested
                onClicked: {
                    if (!root.vehicleContextMatches) {
                        root.testEnabled = false
                        checked = false
                        root.testStatusText = qsTr("活动载具与向导锁定载具不一致，请重新打开向导。")
                        return
                    }
                    root.testEnabled = checked
                    if (checked) {
                        root.testStatusText = qsTr("启用后先等待冷却倒计时，再点击点动。")
                        root.startCooldown(11)
                    } else {
                        root.beginSafeShutdown(false, qsTr("测试已停用"))
                    }
                }
            }

            QGCLabel {
                text:   root.vehicleContextMatches ? qsTr("载具匹配") : qsTr("载具不匹配")
                color:  root.vehicleContextMatches ? root._qgcPal.text : root._qgcPal.warningText
            }

            QGCLabel {
                visible: root.activeVehicle !== null
                text:    root.activeVehicle && root.activeVehicle.armed ? qsTr("已解锁") : qsTr("已上锁")
                color:   root.activeVehicle && root.activeVehicle.armed ? root._qgcPal.text : root._qgcPal.warningText
            }

            QGCLabel {
                visible: root.cooldownRemaining > 0
                text:    qsTr("冷却 %1 秒").arg(root.cooldownRemaining)
                color:   root._qgcPal.warningText
            }

            QGCButton {
                text:       root.disarmRequested ? qsTr("正在上锁") : qsTr("安全结束")
                enabled:    root.vehicleContextMatches
                                && !root.disarmRequested
                                && (root.testEnabled
                                    || root.runningOutput !== -1
                                    || (root.activeVehicle && root.activeVehicle.armed)
                                    || root.directServoBusy())
                onClicked:  root.beginSafeShutdown(false, qsTr("用户结束测试"))
            }

            QGCButton {
                text:    root.activeVehicle && root.activeVehicle.armed ?
                             (root.cooldownRemaining > 0 ? qsTr("冷却中") : qsTr("已准备")) :
                             qsTr("准备下一次测试")
                enabled: root.vehicleContextMatches
                            && root.runningOutput === -1
                            && root.cooldownRemaining === 0
                            && !root.armingRequested
                            && !root.disarmRequested
                onClicked: root.prepareForNextTest()
            }

            QGCLabel { text: qsTr("功率") }

            SpinBox {
                from:           1
                to:             15
                value:          mappingSettings.testPercent
                editable:       true
                onValueModified: mappingSettings.testPercent = value
            }

            QGCLabel { text: qsTr("%") }

            QGCLabel { text: qsTr("时长") }

            SpinBox {
                from:           1
                to:             5
                value:          mappingSettings.testSeconds
                editable:       true
                onValueModified: mappingSettings.testSeconds = value
            }

            QGCLabel { text: qsTr("秒") }
        }

        RowLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelWidth

            QGCLabel {
                text: qsTr("Motor Test发送方式")
            }

            QGCRadioButton {
                text:    qsTr("按测试编号")
                checked: !mappingSettings.useServoFunctionForMotorTest
                enabled: root.runningOutput === -1 && root.cooldownRemaining === 0 && !root.disarmRequested
                onClicked: mappingSettings.useServoFunctionForMotorTest = false
            }

            QGCRadioButton {
                text:    qsTr("按SERVO_FUNCTION映射")
                checked: mappingSettings.useServoFunctionForMotorTest
                enabled: root.runningOutput === -1 && root.cooldownRemaining === 0 && !root.disarmRequested
                onClicked: mappingSettings.useServoFunctionForMotorTest = true
            }

            QGCLabel {
                Layout.fillWidth: true
                elide:            Text.ElideRight
                text:             mappingSettings.useServoFunctionForMotorTest ?
                                      qsTr("点 SERVOx 时读取 SERVOx_FUNCTION；若为 MotorN，则发送 Motor Test N。") :
                                      qsTr("点第 N 行时直接发送 Motor Test N。")
                color:            root._qgcPal.text
            }
        }

        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.5

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCCheckBox {
                    text:       qsTr("高级：直接 SERVO 输出（推进器 PWM）")
                    checked:    root.directServoMode
                    enabled:    root.vehicleContextMatches
                                    && root.runningOutput === -1
                                    && !root.directServoBusy()
                                    && !root.disarmRequested
                    onClicked:  root.directServoMode = checked
                }

                QGCLabel {
                    visible: root.directServoMode
                    text:    qsTr("PWM")
                }

                SpinBox {
                    visible:        root.directServoMode
                    from:           1000
                    to:             3000
                    value:          mappingSettings.directServoPwm
                    editable:       true
                    onValueModified: mappingSettings.directServoPwm = value
                }

                QGCLabel {
                    visible: root.directServoMode
                    text:    qsTr("时长")
                }

                SpinBox {
                    visible:        root.directServoMode
                    from:           1
                    to:             5
                    value:          mappingSettings.directServoSeconds
                    editable:       true
                    onValueModified: mappingSettings.directServoSeconds = value
                }

                QGCLabel {
                    visible: root.directServoMode
                    text:    qsTr("秒")
                }
            }

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCCheckBox {
                    text:       qsTr("舵机点动设置")
                    checked:    root.servoJogMode
                    enabled:    root.vehicleContextMatches
                                    && root.runningOutput === -1
                                    && !root.directServoBusy()
                                    && !root.disarmRequested
                    onClicked:  root.servoJogMode = checked
                }

                QGCLabel {
                    visible: root.servoJogMode
                    text:    qsTr("PWM")
                }

                SpinBox {
                    visible:        root.servoJogMode
                    from:           800
                    to:             2200
                    value:          mappingSettings.servoJogPwm
                    editable:       true
                    onValueModified: mappingSettings.servoJogPwm = value
                }

                QGCLabel {
                    visible: root.servoJogMode
                    text:    qsTr("时长")
                }

                SpinBox {
                    visible:        root.servoJogMode
                    from:           1
                    to:             5
                    value:          mappingSettings.servoJogSeconds
                    editable:       true
                    onValueModified: mappingSettings.servoJogSeconds = value
                }

                QGCLabel {
                    visible: root.servoJogMode
                    text:    qsTr("秒；结束后回到该通道 TRIM")
                }

                QGCLabel {
                    visible: root.directServoMode || root.servoJogMode
                    text:    qsTr("状态：%1").arg(root.directServoStateText())
                    color:   root.directServoState === "idle" ? root._qgcPal.text : root._qgcPal.warningText
                }
            }

            RowLayout {
                Layout.fillWidth:   true
                visible:            (root.directServoMode || root.servoJogMode) && mappingSettings.recoveryPending
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCLabel {
                    Layout.fillWidth: true
                    elide:            Text.ElideRight
                    text:             qsTr("待恢复：载具 %1，SERVO%2，原值 %3，记录于 %4")
                                              .arg(mappingSettings.recoveryVehicleUid.length > 0
                                                       ? mappingSettings.recoveryVehicleUid
                                                       : mappingSettings.recoveryVehicleId)
                                              .arg(mappingSettings.recoveryOutput)
                                              .arg(mappingSettings.recoveryFunction)
                                              .arg(mappingSettings.recoveryTimestamp)
                    color:            root._qgcPal.warningText
                }

                QGCButton {
                    text:       qsTr("恢复异常通道")
                    enabled:    root.vehicleContextMatches
                                    && root.recoveryMatchesSession()
                                    && !root.activeVehicle.armed
                                    && !root.directServoBusy()
                    onClicked:  root.recoverPendingServoFunction()
                }

                QGCButton {
                    text:       root.recoveryDiscardConfirmation ? qsTr("确认忽略记录") : qsTr("忽略旧记录")
                    enabled:    !root.directServoBusy() && root.recoveryJournalCanBeDiscarded()
                    onClicked:  root.discardRecoveryJournal()
                }
            }

        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root.directServoMode
            elide:              Text.ElideRight
            color:              root._qgcPal.warningText
            text:               qsTr("推进器 PWM 直测按物理 SERVO 口发送设定 PWM，不等待 Motor Test 冷却，也不按 SERVO_FUNCTION 映射；结束后回到 1500 PWM。")
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root.servoJogMode
            elide:              Text.ElideRight
            color:              root._qgcPal.warningText
            text:               qsTr("舵机点动按物理 SERVO 口发送独立 PWM，结束后回到该通道 SERVOx_TRIM。两种直发都会临时禁用并恢复被占用通道。")
        }

        GridLayout {
            Layout.fillWidth:   true
            columns:            7
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         ScreenTools.defaultFontPixelHeight * 0.45

            QGCLabel { text: qsTr("SERVO/输出"); font.bold: true }
            QGCLabel { text: qsTr("当前功能"); font.bold: true }
            QGCLabel { text: qsTr("Motor Test"); font.bold: true }
            QGCLabel { text: qsTr("推进器直发PWM"); font.bold: true }
            QGCLabel { text: qsTr("舵机点动"); font.bold: true }
            QGCLabel { text: qsTr("主板接口/线束"); font.bold: true }
            QGCLabel { text: qsTr("实际推进器位置"); font.bold: true }

            Repeater {
                model: 16

                Item {
                    id: rowItem

                    required property int index

                    Layout.fillWidth:   true
                    Layout.columnSpan:  7
                    implicitHeight:     rowLayout.implicitHeight

                    readonly property int outputNumber: index + 1

                    RowLayout {
                        id:             rowLayout
                        anchors.left:   parent.left
                        anchors.right:  parent.right
                        spacing:        ScreenTools.defaultFontPixelWidth

                        QGCLabel {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 13
                            text:                   qsTr("SERVO%1").arg(rowItem.outputNumber)
                        }

                        QGCLabel {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 16
                            text:                   root.servoFunctionText(rowItem.outputNumber)
                            color:                  text === qsTr("Disabled") || text === qsTr("参数缺失") ? root._qgcPal.warningText : root._qgcPal.text
                            elide:                  Text.ElideRight
                        }

                        QGCButton {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 11
                            text:                   root.cooldownRemaining > 0 ? qsTr("冷却") : (root.runningOutput === rowItem.outputNumber ? qsTr("测试中") : qsTr("点动"))
                            enabled:                root.testEnabled
                                                        && root.runningOutput === -1
                                                        && root.cooldownRemaining === 0
                                                        && root.vehicleContextMatches
                                                        && root.activeVehicle.armed
                                                        && !root.disarmRequested
                            onClicked:              root.testOutput(rowItem.outputNumber)
                        }

                        QGCButton {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 11
                            text:                   root.directServoOperation === "thruster"
                                                        && (root.directServoOutput === rowItem.outputNumber || root.directServoPendingOutput === rowItem.outputNumber)
                                                        ? qsTr("直发中") : qsTr("直发PWM")
                            enabled:                root.directServoMode
                                                        && root.vehicleContextMatches
                                                        && root.runningOutput === -1
                                                        && !root.directServoBusy()
                                                        && !mappingSettings.recoveryPending
                                                        && !root.disarmRequested
                            onClicked:              root.startDirectServoTest(rowItem.outputNumber)
                        }

                        QGCButton {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 11
                            text:                   root.directServoOutput === rowItem.outputNumber || root.directServoPendingOutput === rowItem.outputNumber ? qsTr("点动中") : qsTr("点动舵机")
                            enabled:                root.servoJogMode
                                                        && root.vehicleContextMatches
                                                        && root.runningOutput === -1
                                                        && !root.directServoBusy()
                                                        && !mappingSettings.recoveryPending
                                                        && !root.disarmRequested
                            onClicked:              root.startServoJogTest(rowItem.outputNumber)
                        }

                        QGCTextField {
                            id:                     portField
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 20
                            placeholderText:        qsTr("如 MAIN1 / J3-1")
                            text:                   root.portName(rowItem.outputNumber)
                            onEditingFinished:      root.setPortName(rowItem.outputNumber, text)

                            Connections {
                                target: root
                                function onMappingsUpdated() {
                                    portField.text = root.portName(rowItem.outputNumber)
                                }
                            }
                        }

                        QGCComboBox {
                            id:                     positionCombo
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 22
                            model:                  root.positionNames
                            currentIndex:           root.positionIndex(rowItem.outputNumber)
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

        Rectangle {
            Layout.fillWidth:       true
            Layout.preferredHeight: 1
            color:                  root._qgcPal.windowShadeLight
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root.testStatusText.length > 0
            wrapMode:           Text.WordWrap
            text:               root.testStatusText
            color:              root.cooldownRemaining > 0 ? root._qgcPal.warningText : root._qgcPal.text
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            (root.directServoMode || root.servoJogMode) && root.directServoStatusText.length > 0
            wrapMode:           Text.WordWrap
            text:               root.directServoStatusText
            color:              root._qgcPal.warningText
        }

        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.5

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                QGCButton {
                    text:       qsTr("清空记录")
                    onClicked:  root.clearMappings()
                }

                QGCButton {
                    text:       qsTr("导出CSV")
                    onClicked:  exportFileDialog.openForSave()
                }

                Item { Layout.fillWidth: true }
            }

            QGCLabel {
                Layout.fillWidth:   true
                wrapMode:           Text.WordWrap
                text:       qsTr("Motor Test、推进器直发 PWM 和舵机点动是三条独立测试路径。推进器直发结束回到 1500 PWM；舵机点动结束回到 SERVOx_TRIM。两种直发确认参数恢复后才结束。")
                color:      root._qgcPal.text
            }
        }
    }
}

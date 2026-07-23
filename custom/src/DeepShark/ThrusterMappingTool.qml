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

    property var    activeVehicle:      QGroundControl.multiVehicleManager.activeVehicle
    property bool   testEnabled:        false
    property int    runningOutput:      -1
    property int    runningMotorTarget: -1
    property int    motorTestPulsesRemaining: 0
    property bool   directServoMode:    false
    property int    directServoOutput:  -1
    property int    directServoPendingOutput: -1
    property int    cooldownRemaining:  0
    property bool   armingRequested:    false
    property string testStatusText:     ""
    property string directServoStatusText: ""
    readonly property int motorTestCommand: 209
    readonly property int setServoCommand: 183
    readonly property int armDisarmCommand: 400
    readonly property int autopilotComponentId: 1
    readonly property int neutralPwm: 1500
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
        property bool   hasFunctionBackup: false

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

    onClosed: {
        testEnabled = false
        if (root.runningOutput !== -1) {
            root.finishMotorPulse(true)
        }
        if (root.directServoOutput !== -1 || root.directServoPendingOutput !== -1) {
            root.finishDirectServoTest()
        }
    }

    function servoParamName(outputNumber) {
        return "SERVO" + outputNumber + "_FUNCTION"
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

    function backupFunctionValue(outputNumber) {
        switch (outputNumber) {
        case 1: return mappingSettings.backupServo1Function
        case 2: return mappingSettings.backupServo2Function
        case 3: return mappingSettings.backupServo3Function
        case 4: return mappingSettings.backupServo4Function
        case 5: return mappingSettings.backupServo5Function
        case 6: return mappingSettings.backupServo6Function
        case 7: return mappingSettings.backupServo7Function
        case 8: return mappingSettings.backupServo8Function
        case 9: return mappingSettings.backupServo9Function
        case 10: return mappingSettings.backupServo10Function
        case 11: return mappingSettings.backupServo11Function
        case 12: return mappingSettings.backupServo12Function
        case 13: return mappingSettings.backupServo13Function
        case 14: return mappingSettings.backupServo14Function
        case 15: return mappingSettings.backupServo15Function
        case 16: return mappingSettings.backupServo16Function
        default: return 0
        }
    }

    function setBackupFunctionValue(outputNumber, value) {
        switch (outputNumber) {
        case 1: mappingSettings.backupServo1Function = value; break
        case 2: mappingSettings.backupServo2Function = value; break
        case 3: mappingSettings.backupServo3Function = value; break
        case 4: mappingSettings.backupServo4Function = value; break
        case 5: mappingSettings.backupServo5Function = value; break
        case 6: mappingSettings.backupServo6Function = value; break
        case 7: mappingSettings.backupServo7Function = value; break
        case 8: mappingSettings.backupServo8Function = value; break
        case 9: mappingSettings.backupServo9Function = value; break
        case 10: mappingSettings.backupServo10Function = value; break
        case 11: mappingSettings.backupServo11Function = value; break
        case 12: mappingSettings.backupServo12Function = value; break
        case 13: mappingSettings.backupServo13Function = value; break
        case 14: mappingSettings.backupServo14Function = value; break
        case 15: mappingSettings.backupServo15Function = value; break
        case 16: mappingSettings.backupServo16Function = value; break
        }
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
        lines.push(root.csvLine(["Direct SERVO PWM", mappingSettings.directServoPwm]))
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
        lines.push(root.csvLine(["Direct SERVO Test", "Temporarily disables current SERVOx_FUNCTION, sends DO_SET_SERVO PWM, then restores the channel."]))
        return lines.join("\n") + "\n"
    }

    function exportMappingCsv(filename) {
        if (exportController.saveCsv(filename, root.exportCsvText())) {
            root.testStatusText = qsTr("已导出映射 CSV：%1").arg(filename)
        } else {
            root.testStatusText = exportController.lastError
        }
    }

    function backupServoFunctions() {
        if (!root.activeVehicle) {
            root.directServoStatusText = qsTr("未连接载具，无法备份参数。")
            return false
        }

        for (var output = 1; output <= 16; output++) {
            var value = root.servoFunctionValue(output)
            if (value === null) {
                root.directServoStatusText = qsTr("缺少 %1，备份已停止。").arg(root.servoParamName(output))
                return false
            }
            root.setBackupFunctionValue(output, value)
        }

        mappingSettings.hasFunctionBackup = true
        root.directServoStatusText = qsTr("已备份 SERVO1-16_FUNCTION。")
        return true
    }

    function restoreServoFunctions() {
        if (!mappingSettings.hasFunctionBackup) {
            root.directServoStatusText = qsTr("还没有备份，无法恢复。")
            return
        }

        if (root.activeVehicle && root.activeVehicle.armed) {
            root.directServoStatusText = qsTr("请先上锁，再恢复 SERVO 功能参数。")
            return
        }

        root.stopDirectServoTimers()
        for (var output = 1; output <= 16; output++) {
            root.setServoFunctionValue(output, root.backupFunctionValue(output))
        }
        root.directServoOutput = -1
        root.directServoPendingOutput = -1
        root.directServoStatusText = qsTr("已恢复备份的 SERVO1-16_FUNCTION。")
    }

    function startDirectServoTest(outputNumber) {
        root.directServoMode = true

        if (!root.activeVehicle || root.directServoOutput !== -1 || root.directServoPendingOutput !== -1) {
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

        if (!mappingSettings.hasFunctionBackup && !root.backupServoFunctions()) {
            return
        }

        root.directServoPendingOutput = outputNumber
        if (!root.setServoFunctionValue(outputNumber, 0)) {
            root.directServoPendingOutput = -1
            root.directServoStatusText = qsTr("无法临时禁用 %1。").arg(root.servoParamName(outputNumber))
            return
        }

        root.directServoStatusText = qsTr("已临时设置 %1=Disabled，准备直接输出 PWM。").arg(root.servoParamName(outputNumber))
        directServoStartDelay.restart()
    }

    function stopDirectServoTimers() {
        if (directServoStartDelay.running) {
            directServoStartDelay.stop()
        }
        if (directServoStopTimer.running) {
            directServoStopTimer.stop()
        }
    }

    function sendDirectServoPwm(outputNumber, pwm) {
        if (!root.activeVehicle) {
            return
        }

        root.activeVehicle.sendCommand(root.autopilotComponentId, root.setServoCommand, false, outputNumber, pwm)
    }

    function beginDirectServoOutput() {
        if (!root.activeVehicle || root.directServoPendingOutput === -1) {
            root.directServoPendingOutput = -1
            return
        }

        root.directServoOutput = root.directServoPendingOutput
        root.directServoPendingOutput = -1
        root.sendDirectServoPwm(root.directServoOutput, mappingSettings.directServoPwm)
        root.directServoStatusText = qsTr("正在直接输出 SERVO%1 = %2 PWM。").arg(root.directServoOutput).arg(mappingSettings.directServoPwm)
        directServoStopTimer.interval = Math.max(1, mappingSettings.directServoSeconds) * 1000
        directServoStopTimer.restart()
    }

    function finishDirectServoTest() {
        root.stopDirectServoTimers()

        var outputNumber = root.directServoOutput !== -1 ? root.directServoOutput : root.directServoPendingOutput
        root.directServoOutput = -1
        root.directServoPendingOutput = -1

        if (outputNumber === -1) {
            return
        }

        root.sendDirectServoPwm(outputNumber, root.neutralPwm)
        if (mappingSettings.hasFunctionBackup) {
            root.setServoFunctionValue(outputNumber, root.backupFunctionValue(outputNumber))
        }
        root.directServoStatusText = qsTr("SERVO%1 直接输出结束，已回中并恢复该路功能参数。").arg(outputNumber)
    }

    function testOutput(outputNumber) {
        if (!root.activeVehicle || root.runningOutput !== -1 || root.cooldownRemaining > 0) {
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
        if (!root.activeVehicle || root.runningOutput === -1) {
            motorTestPulseTimer.stop()
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
                    qsTr("SERVO%1 -> Motor%2 点动结束，等待飞控冷却。").arg(outputNumber).arg(motorTarget) :
                    qsTr("Motor Test %1 点动结束，等待飞控冷却。").arg(motorTarget)
    }

    function prepareForNextTest() {
        if (!root.activeVehicle) {
            root.testStatusText = qsTr("未连接载具，无法准备测试。")
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
        id:         directServoStartDelay
        interval:   700
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
            if (command === root.armDisarmCommand && root.armingRequested) {
                if (ackResult !== 0) {
                    root.armingRequested = false
                    root.testStatusText = qsTr("飞控拒绝解锁。请检查安全开关、模式和故障信息。")
                }
                return
            }

            if (command === root.setServoCommand && ackResult !== 0) {
                root.directServoStatusText = qsTr("飞控拒绝 DO_SET_SERVO。该通道可能仍被功能占用，或固件不允许直接输出。")
                return
            }

            if (command !== root.motorTestCommand) {
                return
            }

            if (ackResult !== 0) {
                if (motorTestPulseTimer.running) {
                    motorTestPulseTimer.stop()
                }
                root.runningOutput = -1
                root.runningMotorTarget = -1
                root.motorTestPulsesRemaining = 0
                root.startCooldown(11)
                root.testStatusText = qsTr("飞控拒绝 Motor Test。若刚测试过，请等待冷却倒计时结束后再试。")
            }
        }

        function onArmedChanged(armed) {
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
        spacing:    ScreenTools.defaultFontPixelHeight

        QGCLabel {
            Layout.fillWidth:   true
            wrapMode:           Text.WordWrap
            color:              root._qgcPal.warningText
            text:               qsTr("逐路点动板载输出，现场记录主板接口/线束和实际推进器位置。当前功能列只读取 SERVOx_FUNCTION，不会写入飞控参数。请确认机器人固定、推进器周围无人员和障碍物。")
        }

        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.5

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth * 2

                QGCCheckBox {
                    id:         enableTestCheck
                    text:       qsTr("启用测试")
                    checked:    root.testEnabled
                    enabled:    root.activeVehicle !== null
                    onClicked: {
                        if (!root.activeVehicle) {
                            root.testEnabled = false
                            checked = false
                            return
                        }
                        root.testEnabled = checked
                        if (checked) {
                            root.testStatusText = qsTr("启用后先等待冷却倒计时，再点击点动。")
                            root.startCooldown(11)
                        }
                    }
                }

                QGCLabel {
                    text:   root.activeVehicle ? qsTr("已连接载具") : qsTr("未连接载具")
                    color:  root.activeVehicle ? root._qgcPal.text : root._qgcPal.warningText
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

                Item { Layout.fillWidth: true }

                QGCButton {
                    text:    root.activeVehicle && root.activeVehicle.armed ?
                                 (root.cooldownRemaining > 0 ? qsTr("冷却中") : qsTr("已准备")) :
                                 qsTr("准备下一次测试")
                    enabled: root.activeVehicle !== null && root.runningOutput === -1 && root.cooldownRemaining === 0 && !root.armingRequested
                    onClicked: root.prepareForNextTest()
                }
            }

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth

                Item { Layout.fillWidth: true }

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
                    to:             3
                    value:          mappingSettings.testSeconds
                    editable:       true
                    onValueModified: mappingSettings.testSeconds = value
                }

                QGCLabel { text: qsTr("秒") }
            }
        }

        ColumnLayout {
            Layout.fillWidth:   true
            spacing:            ScreenTools.defaultFontPixelHeight * 0.35

            RowLayout {
                Layout.fillWidth:   true
                spacing:            ScreenTools.defaultFontPixelWidth * 2

                QGCLabel {
                    text: qsTr("Motor Test发送方式")
                }

                QGCRadioButton {
                    text:    qsTr("按测试编号")
                    checked: !mappingSettings.useServoFunctionForMotorTest
                    enabled: root.runningOutput === -1 && root.cooldownRemaining === 0
                    onClicked: mappingSettings.useServoFunctionForMotorTest = false
                }

                QGCRadioButton {
                    text:    qsTr("按SERVO_FUNCTION映射")
                    checked: mappingSettings.useServoFunctionForMotorTest
                    enabled: root.runningOutput === -1 && root.cooldownRemaining === 0
                    onClicked: mappingSettings.useServoFunctionForMotorTest = true
                }

                Item { Layout.fillWidth: true }
            }

            QGCLabel {
                Layout.fillWidth: true
                wrapMode:         Text.WordWrap
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
                spacing:            ScreenTools.defaultFontPixelWidth * 2

                QGCCheckBox {
                    text:       qsTr("高级：直接 SERVO 输出")
                    checked:    root.directServoMode
                    enabled:    root.activeVehicle !== null && root.runningOutput === -1 && root.directServoOutput === -1 && root.directServoPendingOutput === -1
                    onClicked:  root.directServoMode = checked
                }

                QGCButton {
                    text:       qsTr("备份SERVO功能")
                    visible:    root.directServoMode
                    enabled:    root.activeVehicle !== null && (!root.activeVehicle.armed) && root.directServoOutput === -1 && root.directServoPendingOutput === -1
                    onClicked:  root.backupServoFunctions()
                }

                QGCButton {
                    text:       qsTr("恢复备份")
                    visible:    root.directServoMode
                    enabled:    root.activeVehicle !== null && (!root.activeVehicle.armed) && mappingSettings.hasFunctionBackup && root.directServoOutput === -1 && root.directServoPendingOutput === -1
                    onClicked:  root.restoreServoFunctions()
                }

                QGCLabel {
                    visible: root.directServoMode
                    text:    mappingSettings.hasFunctionBackup ? qsTr("已备份") : qsTr("未备份")
                    color:   mappingSettings.hasFunctionBackup ? root._qgcPal.text : root._qgcPal.warningText
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth:   true
                visible:            root.directServoMode
                spacing:            ScreenTools.defaultFontPixelWidth

                Item { Layout.fillWidth: true }

                QGCLabel {
                    text: qsTr("PWM")
                }

                SpinBox {
                    from:           1000
                    to:             2000
                    value:          mappingSettings.directServoPwm
                    editable:       true
                    onValueModified: mappingSettings.directServoPwm = value
                }

                QGCLabel {
                    text: qsTr("时长")
                }

                SpinBox {
                    from:           1
                    to:             3
                    value:          mappingSettings.directServoSeconds
                    editable:       true
                    onValueModified: mappingSettings.directServoSeconds = value
                }

                QGCLabel {
                    text: qsTr("秒")
                }
            }
        }

        QGCLabel {
            Layout.fillWidth:   true
            visible:            root.directServoMode
            wrapMode:           Text.WordWrap
            color:              root._qgcPal.warningText
            text:               qsTr("直接 SERVO 输出会临时将当前通道 SERVOx_FUNCTION 设为 Disabled，发送 DO_SET_SERVO PWM，结束后回中并恢复该通道。请保持飞控上锁，只在确认推进器周围安全时使用。")
        }

        GridLayout {
            Layout.fillWidth:   true
            columns:            6
            columnSpacing:      ScreenTools.defaultFontPixelWidth
            rowSpacing:         ScreenTools.defaultFontPixelHeight * 0.45

            QGCLabel { text: qsTr("SERVO/输出"); font.bold: true }
            QGCLabel { text: qsTr("当前功能"); font.bold: true }
            QGCLabel { text: qsTr("Motor Test"); font.bold: true }
            QGCLabel { text: qsTr("直测SERVO"); font.bold: true }
            QGCLabel { text: qsTr("主板接口/线束"); font.bold: true }
            QGCLabel { text: qsTr("实际推进器位置"); font.bold: true }

            Repeater {
                model: 16

                Item {
                    id: rowItem

                    required property int index

                    Layout.fillWidth:   true
                    Layout.columnSpan:  6
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
                            enabled:                root.testEnabled && root.runningOutput === -1 && root.cooldownRemaining === 0 && root.activeVehicle !== null && root.activeVehicle.armed
                            onClicked:              root.testOutput(rowItem.outputNumber)
                        }

                        QGCButton {
                            Layout.preferredWidth:  ScreenTools.defaultFontPixelWidth * 11
                            text:                   root.directServoOutput === rowItem.outputNumber || root.directServoPendingOutput === rowItem.outputNumber ? qsTr("输出中") : qsTr("直测")
                            enabled:                root.activeVehicle !== null && root.runningOutput === -1 && root.directServoOutput === -1 && root.directServoPendingOutput === -1
                            onClicked:              root.startDirectServoTest(rowItem.outputNumber)
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
            visible:            root.directServoMode && root.directServoStatusText.length > 0
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
                text:       qsTr("普通 Motor Test 不写飞控参数；高级直测会临时改当前 SERVOx_FUNCTION 并自动恢复，接口/位置记录保存在本机 QGC 设置中。")
                color:      root._qgcPal.text
            }
        }
    }
}

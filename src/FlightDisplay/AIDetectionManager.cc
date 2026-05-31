/****************************************************************************
 *
 * Optional AI detection process manager for QGC.
 *
 ****************************************************************************/

#include "AIDetectionManager.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QProcess>
#include <QtCore/QSettings>
#include <QtCore/QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCheckTimeoutMs = 15000;
constexpr const char *kSettingsGroup = "AIDetection";
constexpr const char *kPythonPathKey = "PythonPath";
constexpr const char *kModelPathKey = "ModelPath";
constexpr const char *kDeviceKey = "Device";
constexpr const char *kConfidenceKey = "Confidence";
constexpr const char *kImageSizeKey = "ImageSize";
constexpr const char *kMaxFpsKey = "MaxFps";
constexpr const char *kUdpPortKey = "UdpPort";

QString cleanPath(const QString &path)
{
    QString cleaned = path.trimmed();
    if (cleaned.startsWith(QStringLiteral("file:///"))) {
        cleaned = cleaned.mid(8);
    }
    return QDir::fromNativeSeparators(cleaned);
}
}

AIDetectionManager::AIDetectionManager(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    _pythonPathValue = settings.value(QString::fromLatin1(kPythonPathKey)).toString();
    _modelPathValue = settings.value(QString::fromLatin1(kModelPathKey)).toString();
    _device = settings.value(QString::fromLatin1(kDeviceKey)).toString();
    _confidence = settings.value(QString::fromLatin1(kConfidenceKey), _confidence).toDouble();
    _imageSize = settings.value(QString::fromLatin1(kImageSizeKey), _imageSize).toInt();
    _maxFps = settings.value(QString::fromLatin1(kMaxFpsKey), _maxFps).toDouble();
    _udpPort = settings.value(QString::fromLatin1(kUdpPortKey), _udpPort).toInt();
    settings.endGroup();
    _statusText = tr("AI detection is stopped");
    _checkReport = tr("AI environment has not been checked");
}

AIDetectionManager::~AIDetectionManager()
{
    _shuttingDown = true;
    _deleteProcess(_detectProcess);
    _deleteProcess(_checkProcess);
}

bool AIDetectionManager::running() const
{
    return _detectProcess && _detectProcess->state() != QProcess::NotRunning;
}

void AIDetectionManager::setPythonPath(const QString &pythonPath)
{
    const QString cleaned = cleanPath(pythonPath);
    if (_pythonPathValue == cleaned) {
        return;
    }
    _pythonPathValue = cleaned;
    _writeSetting(QString::fromLatin1(kPythonPathKey), _pythonPathValue);
    emit pythonPathChanged();
}

void AIDetectionManager::setModelPath(const QString &modelPath)
{
    const QString cleaned = cleanPath(modelPath);
    if (_modelPathValue == cleaned) {
        return;
    }
    _modelPathValue = cleaned;
    _writeSetting(QString::fromLatin1(kModelPathKey), _modelPathValue);
    emit modelPathChanged();
}

void AIDetectionManager::setDevice(const QString &device)
{
    const QString cleaned = device.trimmed();
    if (_device == cleaned) {
        return;
    }
    _device = cleaned;
    _writeSetting(QString::fromLatin1(kDeviceKey), _device);
    emit deviceChanged();
}

void AIDetectionManager::setConfidence(double confidence)
{
    if (!std::isfinite(confidence)) {
        return;
    }
    const double clamped = std::clamp(confidence, 0.01, 1.0);
    if (qFuzzyCompare(_confidence, clamped)) {
        return;
    }
    _confidence = clamped;
    _writeSetting(QString::fromLatin1(kConfidenceKey), _confidence);
    emit confidenceChanged();
}

void AIDetectionManager::setImageSize(int imageSize)
{
    const int clamped = std::clamp(imageSize, 64, 4096);
    if (_imageSize == clamped) {
        return;
    }
    _imageSize = clamped;
    _writeSetting(QString::fromLatin1(kImageSizeKey), _imageSize);
    emit imageSizeChanged();
}

void AIDetectionManager::setMaxFps(double maxFps)
{
    if (!std::isfinite(maxFps)) {
        return;
    }
    const double clamped = std::clamp(maxFps, 0.0, 60.0);
    if (qFuzzyCompare(_maxFps + 1.0, clamped + 1.0)) {
        return;
    }
    _maxFps = clamped;
    _writeSetting(QString::fromLatin1(kMaxFpsKey), _maxFps);
    emit maxFpsChanged();
}

void AIDetectionManager::setUdpPort(int udpPort)
{
    const int clamped = std::clamp(udpPort, 1024, 65535);
    if (_udpPort == clamped) {
        return;
    }
    _udpPort = clamped;
    _writeSetting(QString::fromLatin1(kUdpPortKey), _udpPort);
    emit udpPortChanged();
}

QString AIDetectionManager::toolsPath() const
{
    return _toolsPath();
}

void AIDetectionManager::checkEnvironment()
{
    if (_checkProcess && _checkProcess->state() != QProcess::NotRunning) {
        _setCheckReport(tr("AI environment check is already running"));
        return;
    }

    const QString validation = _validateConfiguration();
    if (!validation.isEmpty()) {
        _setCheckReport(validation);
        return;
    }

    _deleteProcess(_checkProcess);
    _checkProcess = new QProcess(this);
    _checkProcess->setProgram(_pythonPath());
    _checkProcess->setArguments({
        QStringLiteral("-c"),
        QStringLiteral("import sys; print('Python ' + sys.version.split()[0]); import ultralytics; print('ultralytics OK'); import torch; print('torch OK')")
    });
    _checkProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(_checkProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { _checkFinished(exitCode); });

    _setCheckReport(tr("Checking AI environment..."));
    _checkProcess->start();
    if (!_checkProcess->waitForStarted(3000)) {
        _setCheckReport(tr("Failed to start Python: %1").arg(_checkProcess->errorString()));
        return;
    }

    QTimer::singleShot(kCheckTimeoutMs, this, [this]() {
        if (_checkProcess && _checkProcess->state() != QProcess::NotRunning) {
            _checkProcess->kill();
            _setCheckReport(tr("AI environment check timed out"));
        }
    });
}

void AIDetectionManager::startDetection()
{
    if (running()) {
        _setStatusText(tr("AI detection is already running"));
        return;
    }

    const QString validation = _validateConfiguration();
    if (!validation.isEmpty()) {
        _setStatusText(validation);
        _setCheckReport(validation);
        return;
    }

    _deleteProcess(_detectProcess);
    _detectProcess = new QProcess(this);
    _detectProcess->setProgram(_pythonPath());
    _detectProcess->setArguments(_detectionArguments());
    _detectProcess->setWorkingDirectory(QDir::currentPath());
    _detectProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(_detectProcess, &QProcess::readyReadStandardOutput, this, &AIDetectionManager::_readDetectOutput);
    connect(_detectProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus) { _detectFinished(exitCode); });
    connect(_detectProcess, &QProcess::errorOccurred, this, &AIDetectionManager::_detectError);

    _setStatusText(tr("Starting AI detection..."));
    _detectProcess->start();
    if (!_detectProcess->waitForStarted(5000)) {
        _setStatusText(tr("Failed to start AI detection: %1").arg(_detectProcess->errorString()));
        return;
    }

    emit runningChanged();
    _setStatusText(tr("AI detection is running"));
}

void AIDetectionManager::stopDetection()
{
    if (!running()) {
        _setStatusText(tr("AI detection is stopped"));
        return;
    }

    _detectProcess->terminate();
    if (!_detectProcess->waitForFinished(5000)) {
        _detectProcess->kill();
        _detectProcess->waitForFinished(2000);
    }
    _setStatusText(tr("AI detection is stopped"));
    emit runningChanged();
}

void AIDetectionManager::restartDetection()
{
    stopDetection();
    startDetection();
}

void AIDetectionManager::saveSettings()
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(QString::fromLatin1(kPythonPathKey), _pythonPathValue);
    settings.setValue(QString::fromLatin1(kModelPathKey), _modelPathValue);
    settings.setValue(QString::fromLatin1(kDeviceKey), _device);
    settings.setValue(QString::fromLatin1(kConfidenceKey), _confidence);
    settings.setValue(QString::fromLatin1(kImageSizeKey), _imageSize);
    settings.setValue(QString::fromLatin1(kMaxFpsKey), _maxFps);
    settings.setValue(QString::fromLatin1(kUdpPortKey), _udpPort);
    settings.endGroup();
    settings.sync();
    _setStatusText(tr("AI settings saved"));
}

void AIDetectionManager::_checkFinished(int exitCode)
{
    const QString output = QString::fromLocal8Bit(_checkProcess->readAllStandardOutput()).trimmed();
    if (exitCode == 0) {
        _setCheckReport(tr("%1\nModel: %2\nTools: %3").arg(output, _modelPath(), _toolsPath()));
    } else {
        _setCheckReport(tr("AI environment check failed:\n%1").arg(output));
    }
}

void AIDetectionManager::_detectFinished(int exitCode)
{
    const QString output = QString::fromLocal8Bit(_detectProcess->readAllStandardOutput()).trimmed();
    _setStatusText(output.isEmpty()
        ? tr("AI detection exited with code %1").arg(exitCode)
        : tr("AI detection exited with code %1: %2").arg(exitCode).arg(output.right(300)));
    emit runningChanged();
}

void AIDetectionManager::_detectError()
{
    if (_detectProcess) {
        _setStatusText(tr("AI detection process error: %1").arg(_detectProcess->errorString()));
    }
}

void AIDetectionManager::_readDetectOutput()
{
    const QString output = QString::fromLocal8Bit(_detectProcess->readAllStandardOutput()).trimmed();
    if (!output.isEmpty()) {
        _setStatusText(output.right(300));
    }
}

QString AIDetectionManager::_pythonPath() const
{
    return cleanPath(_pythonPathValue);
}

QString AIDetectionManager::_toolsPath() const
{
    const QStringList candidates {
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("ai_detection")),
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("tools/ai_detection")),
        QDir(QDir::currentPath()).filePath(QStringLiteral("tools/ai_detection")),
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("run_yolo_to_qgc_auto.py")))) {
            return QDir::cleanPath(candidate);
        }
    }
    return candidates.last();
}

QString AIDetectionManager::_modelPath() const
{
    return cleanPath(_modelPathValue);
}

QString AIDetectionManager::_autoLauncherPath() const
{
    return QDir(_toolsPath()).filePath(QStringLiteral("run_yolo_to_qgc_auto.py"));
}

QString AIDetectionManager::_settingsFilePath() const
{
    QSettings settings;
    return settings.fileName();
}

QString AIDetectionManager::_validateConfiguration() const
{
    if (_pythonPath().isEmpty() || !QFileInfo::exists(_pythonPath())) {
        return tr("Python path is not configured or does not exist");
    }
    if (_modelPath().isEmpty() || !QFileInfo::exists(_modelPath())) {
        return tr("Model path is not configured or does not exist");
    }
    if (!QFileInfo::exists(_autoLauncherPath())) {
        return tr("AI launcher script was not found: %1").arg(_autoLauncherPath());
    }
    return {};
}

QStringList AIDetectionManager::_detectionArguments() const
{
    QStringList args {
        _autoLauncherPath(),
        QStringLiteral("--model"), _modelPath(),
        QStringLiteral("--port"), QString::number(_udpPort),
        QStringLiteral("--imgsz"), QString::number(_imageSize),
        QStringLiteral("--conf"), QString::number(_confidence, 'f', 2),
        QStringLiteral("--max-fps"), QString::number(_maxFps, 'f', 1),
        QStringLiteral("--settings-file"), _settingsFilePath(),
    };

    if (!_device.isEmpty()) {
        args << QStringLiteral("--device") << _device;
    }
    return args;
}

void AIDetectionManager::_setStatusText(const QString &statusText)
{
    if (_shuttingDown) {
        return;
    }
    if (_statusText == statusText) {
        return;
    }
    _statusText = statusText;
    emit statusTextChanged();
}

void AIDetectionManager::_setCheckReport(const QString &checkReport)
{
    if (_shuttingDown) {
        return;
    }
    if (_checkReport == checkReport) {
        return;
    }
    _checkReport = checkReport;
    emit checkReportChanged();
}

void AIDetectionManager::_deleteProcess(QProcess *&process)
{
    if (!process) {
        return;
    }

    disconnect(process, nullptr, this, nullptr);
    if (process->state() != QProcess::NotRunning) {
        process->terminate();
        if (!process->waitForFinished(3000)) {
            process->kill();
            process->waitForFinished(1000);
        }
    }
    delete process;
    process = nullptr;
}

void AIDetectionManager::_writeSetting(const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(key, value);
    settings.endGroup();
    settings.sync();
}

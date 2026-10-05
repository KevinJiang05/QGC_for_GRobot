/****************************************************************************
 *
 * Optional AI detection process manager for QGC.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QProcess>
#include <QtCore/QString>
#include <QtCore/QVariant>

class AIDetectionManager : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool overlayEnabled READ overlayEnabled WRITE setOverlayEnabled NOTIFY overlayEnabledChanged)
    Q_PROPERTY(QString pythonPath READ pythonPath WRITE setPythonPath NOTIFY pythonPathChanged)
    Q_PROPERTY(QString modelPath READ modelPath WRITE setModelPath NOTIFY modelPathChanged)
    Q_PROPERTY(QString device READ device WRITE setDevice NOTIFY deviceChanged)
    Q_PROPERTY(double confidence READ confidence WRITE setConfidence NOTIFY confidenceChanged)
    Q_PROPERTY(int imageSize READ imageSize WRITE setImageSize NOTIFY imageSizeChanged)
    Q_PROPERTY(double maxFps READ maxFps WRITE setMaxFps NOTIFY maxFpsChanged)
    Q_PROPERTY(int udpPort READ udpPort WRITE setUdpPort NOTIFY udpPortChanged)
    Q_PROPERTY(QString toolsPath READ toolsPath NOTIFY toolsPathChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString checkReport READ checkReport NOTIFY checkReportChanged)

public:
    explicit AIDetectionManager(QObject *parent = nullptr);
    ~AIDetectionManager();

    bool running() const;
    bool overlayEnabled() const { return _overlayEnabled; }
    void setOverlayEnabled(bool overlayEnabled);
    QString pythonPath() const { return _pythonPathValue; }
    void setPythonPath(const QString &pythonPath);
    QString modelPath() const { return _modelPathValue; }
    void setModelPath(const QString &modelPath);
    QString device() const { return _device; }
    void setDevice(const QString &device);
    double confidence() const { return _confidence; }
    void setConfidence(double confidence);
    int imageSize() const { return _imageSize; }
    void setImageSize(int imageSize);
    double maxFps() const { return _maxFps; }
    void setMaxFps(double maxFps);
    int udpPort() const { return _udpPort; }
    void setUdpPort(int udpPort);
    QString toolsPath() const;
    QString statusText() const { return _statusText; }
    QString checkReport() const { return _checkReport; }

    Q_INVOKABLE void checkEnvironment();
    Q_INVOKABLE void startDetection();
    Q_INVOKABLE void stopDetection();
    Q_INVOKABLE void restartDetection();
    Q_INVOKABLE void saveSettings();

signals:
    void runningChanged();
    void overlayEnabledChanged();
    void pythonPathChanged();
    void modelPathChanged();
    void deviceChanged();
    void confidenceChanged();
    void imageSizeChanged();
    void maxFpsChanged();
    void udpPortChanged();
    void toolsPathChanged();
    void statusTextChanged();
    void checkReportChanged();

private slots:
    void _checkFinished(int exitCode);
    void _detectFinished(int exitCode);
    void _detectError(QProcess::ProcessError error);
    void _detectStarted();
    void _readDetectOutput();

private:
    QString _pythonPath() const;
    QString _toolsPath() const;
    QString _modelPath() const;
    QString _autoLauncherPath() const;
    QString _settingsFilePath() const;
    QString _validateConfiguration() const;
    QStringList _detectionArguments() const;
    void _requestDetectionStop(bool restartAfterStop);
    void _setStatusText(const QString &statusText);
    void _setCheckReport(const QString &checkReport);
    void _deleteProcess(QProcess *&process);
    void _writeSetting(const QString &key, const QVariant &value);

    QProcess *_detectProcess = nullptr;
    QProcess *_checkProcess = nullptr;
    bool _shuttingDown = false;
    bool _restartPending = false;
    bool _stopRequested = false;
    bool _overlayEnabled = true;
    QString _pythonPathValue;
    QString _modelPathValue;
    QString _device;
    double _confidence = 0.25;
    int _imageSize = 640;
    double _maxFps = 8.0;
    int _udpPort = 57610;
    QString _statusText = QStringLiteral("AI detection is stopped");
    QString _checkReport = QStringLiteral("AI environment has not been checked");
};

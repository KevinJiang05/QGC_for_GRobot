/****************************************************************************
 *
 * DeepShark custom QGroundControl build plugin.
 *
 ****************************************************************************/

#include "DeepSharkPlugin.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/qapplicationstatic.h>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/qqml.h>

#include "AIDetectionManager.h"
#include "AIDetectionReceiver.h"
#include "DeepSharkAuvController.h"
#include "ThrusterMappingExportController.h"
#include "ThrusterDirectControlController.h"
#include "DeepSharkVideoController.h"
#include "DeepSharkVideoSettings.h"
#include "DeepSharkConnectionMonitor.h"
#include "MultiVehicleManager.h"
#include "QGCApplication.h"

Q_APPLICATION_STATIC(DeepSharkPlugin, _deepSharkPluginInstance);

DeepSharkOptions::DeepSharkOptions(QObject *parent)
    : QGCOptions(parent)
{
}

DeepSharkPlugin::DeepSharkPlugin(QObject *parent)
    : QGCCorePlugin(parent)
    , _options(this)
{
}

DeepSharkPlugin::~DeepSharkPlugin()
{
}

QGCCorePlugin *DeepSharkPlugin::instance()
{
    return _deepSharkPluginInstance();
}

void DeepSharkPlugin::init()
{
    // Owned by the plugin so they are destroyed with the application instead of after it.
    if (!_auvController) {
        _auvController = new DeepSharkAuvController(this);
        _videoSettings = new DeepSharkVideoSettings(this);
    }
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkAuvController", _auvController);
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkVideoSettings", _videoSettings);
    auto *connectionMonitor = DeepSharkConnectionMonitor::instance();
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkConnectionMonitor", connectionMonitor);
    connectionMonitor->start();
    qmlRegisterType<DeepSharkVideoController>("DeepShark", 1, 0, "DeepSharkVideoController");
    qmlRegisterType<ThrusterMappingExportController>("DeepShark", 1, 0, "ThrusterMappingExportController");
    qmlRegisterType<ThrusterDirectControlController>("DeepShark", 1, 0, "ThrusterDirectControlController");
    _initAIDetection();
    // Unit tests call init() repeatedly, so the connection is unique. Every change to another vehicle
    // checks the record again.
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged, this,
            &DeepSharkPlugin::_activeVehicleChanged, Qt::UniqueConnection);
}

void DeepSharkPlugin::_activeVehicleChanged(Vehicle *vehicle)
{
    if (!vehicle) {
        return;
    }
    // The tool that owns the record is closed after a restart, so nothing else would show it. The record
    // is local, so this does not wait for parameters, which an armed or failing vehicle may never deliver.
    const QString notice = ThrusterDirectControlController::pendingRecoveryNotice();
    if (!notice.isEmpty()) {
        qgcApp()->showAppMessage(notice, tr("输出测试未完成"));
    }
}

void DeepSharkPlugin::_initAIDetection()
{
    // Unit tests call init() once per QML test case, so the AI owners are created only once.
    if (!_aiDetectionManager) {
        QCoreApplication *app = QCoreApplication::instance();
        auto *receiver = new AIDetectionReceiver(app);
        auto *manager = new AIDetectionManager(app);
        receiver->setPort(static_cast<quint16>(manager->udpPort()));
        connect(manager, &AIDetectionManager::udpPortChanged, receiver, [manager, receiver]() {
            if (!manager->running()) {
                receiver->setPort(static_cast<quint16>(manager->udpPort()));
            }
        });
        connect(manager, &AIDetectionManager::runningChanged, receiver, [manager, receiver]() {
            if (manager->running()) {
                receiver->setPort(static_cast<quint16>(manager->udpPort()));
            } else {
                receiver->clearDetections();
            }
        });
        connect(app, &QCoreApplication::aboutToQuit, receiver, [receiver]() {
            receiver->setEnabled(false);
        });
        connect(app, &QCoreApplication::aboutToQuit, manager, &AIDetectionManager::stopDetection);
        receiver->setEnabled(true);
        _aiDetectionReceiver = receiver;
        _aiDetectionManager = manager;
    }

    qmlRegisterSingletonInstance("DeepShark", 1, 0, "AIDetectionReceiver", _aiDetectionReceiver.data());
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "AIDetectionManager", _aiDetectionManager.data());
}

void DeepSharkPlugin::cleanup()
{
    if (_qmlEngine && _selector) {
        _qmlEngine->removeUrlInterceptor(_selector);
    }

    delete _selector;
    _selector = nullptr;
    _qmlEngine = nullptr;
}

QGCOptions *DeepSharkPlugin::options()
{
    return &_options;
}

QQmlApplicationEngine *DeepSharkPlugin::createQmlApplicationEngine(QObject *parent)
{
    _qmlEngine = QGCCorePlugin::createQmlApplicationEngine(parent);
    _selector = new DeepSharkOverrideInterceptor();
    _qmlEngine->addUrlInterceptor(_selector);

    return _qmlEngine;
}

DeepSharkOverrideInterceptor::DeepSharkOverrideInterceptor()
    : QQmlAbstractUrlInterceptor()
{
}

void DeepSharkPlugin::destroyQmlApplicationEngine(QQmlApplicationEngine* qmlEngine)
{
    if (_qmlEngine == qmlEngine) {
        cleanup();
    }
    QGCCorePlugin::destroyQmlApplicationEngine(qmlEngine);
}

QUrl DeepSharkOverrideInterceptor::intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type)
{
    switch (type) {
    using DataType = QQmlAbstractUrlInterceptor::DataType;
    case DataType::QmlFile:
    case DataType::UrlString:
        if (url.scheme() == QStringLiteral("qrc")) {
            const QString overrideResource = QStringLiteral(":/Custom%1").arg(url.path());
            if (QFile::exists(overrideResource)) {
                QUrl result;
                result.setScheme(QStringLiteral("qrc"));
                result.setPath(overrideResource.mid(1));
                return result;
            }
        }
        break;
    default:
        break;
    }

    return url;
}

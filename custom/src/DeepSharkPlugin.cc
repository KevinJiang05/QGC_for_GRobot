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
    static DeepSharkAuvController auvController;
    static DeepSharkVideoSettings videoSettings;
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkAuvController", &auvController);
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkVideoSettings", &videoSettings);
    auto *connectionMonitor = DeepSharkConnectionMonitor::instance();
    qmlRegisterSingletonInstance("DeepShark", 1, 0, "DeepSharkConnectionMonitor", connectionMonitor);
    connectionMonitor->start();
    qmlRegisterType<DeepSharkVideoController>("DeepShark", 1, 0, "DeepSharkVideoController");
    qmlRegisterType<ThrusterMappingExportController>("DeepShark", 1, 0, "ThrusterMappingExportController");
    qmlRegisterType<ThrusterDirectControlController>("DeepShark", 1, 0, "ThrusterDirectControlController");
    _initAIDetection();
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

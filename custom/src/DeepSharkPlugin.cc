/****************************************************************************
 *
 * DeepShark custom QGroundControl build plugin.
 *
 ****************************************************************************/

#include "DeepSharkPlugin.h"

#include <QtCore/QFile>
#include <QtCore/qapplicationstatic.h>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/qqml.h>

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

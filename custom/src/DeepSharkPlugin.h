/****************************************************************************
 *
 * DeepShark custom QGroundControl build plugin.
 *
 ****************************************************************************/

#pragma once

#include "QGCCorePlugin.h"
#include "QGCOptions.h"

#include <QtQml/QQmlAbstractUrlInterceptor>

class QQmlApplicationEngine;

class DeepSharkOverrideInterceptor;

class DeepSharkOptions : public QGCOptions
{
public:
    explicit DeepSharkOptions(QObject *parent = nullptr);

    bool checkFirmwareVersion() const final { return false; }
};

class DeepSharkPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit DeepSharkPlugin(QObject *parent = nullptr);
    ~DeepSharkPlugin();

    static QGCCorePlugin *instance();

    void init() final;
    void cleanup() final;
    QGCOptions *options() final;
    QString stableVersionCheckFileUrl() const final { return QString(); }
    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) final;

private:
    DeepSharkOptions _options;
    QQmlApplicationEngine *_qmlEngine = nullptr;
    DeepSharkOverrideInterceptor *_selector = nullptr;
};

class DeepSharkOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    DeepSharkOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};

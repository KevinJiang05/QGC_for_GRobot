/****************************************************************************
 *
 * DeepShark custom QGroundControl build plugin.
 *
 ****************************************************************************/

#pragma once

#include "QGCCorePlugin.h"

#include <QtQml/QQmlAbstractUrlInterceptor>

class QQmlApplicationEngine;

class DeepSharkOverrideInterceptor;

class DeepSharkPlugin : public QGCCorePlugin
{
    Q_OBJECT

public:
    explicit DeepSharkPlugin(QObject *parent = nullptr);
    ~DeepSharkPlugin();

    static QGCCorePlugin *instance();

    void init() final;
    void cleanup() final;
    QQmlApplicationEngine *createQmlApplicationEngine(QObject *parent) final;

private:
    QQmlApplicationEngine *_qmlEngine = nullptr;
    DeepSharkOverrideInterceptor *_selector = nullptr;
};

class DeepSharkOverrideInterceptor : public QQmlAbstractUrlInterceptor
{
public:
    DeepSharkOverrideInterceptor();

    QUrl intercept(const QUrl &url, QQmlAbstractUrlInterceptor::DataType type) final;
};

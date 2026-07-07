/****************************************************************************
 *
 * DeepShark thruster mapping export helper.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class ThrusterMappingExportController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit ThrusterMappingExportController(QObject *parent = nullptr);

    QString lastError() const { return _lastError; }

    Q_INVOKABLE bool saveCsv(const QString &filename, const QString &csvText);

signals:
    void lastErrorChanged();

private:
    void _setLastError(const QString &error);

    QString _lastError;
};

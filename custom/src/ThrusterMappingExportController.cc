/****************************************************************************
 *
 * DeepShark thruster mapping export helper.
 *
 ****************************************************************************/

#include "ThrusterMappingExportController.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>

ThrusterMappingExportController::ThrusterMappingExportController(QObject *parent)
    : QObject(parent)
{
}

bool ThrusterMappingExportController::saveCsv(const QString &filename, const QString &csvText)
{
    if (filename.isEmpty()) {
        _setLastError(tr("Export filename is empty."));
        return false;
    }

    QString exportFilename = filename;
    if (!QFileInfo(exportFilename).fileName().contains(QLatin1Char('.'))) {
        exportFilename += QStringLiteral(".csv");
    }

    QFile file(exportFilename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        _setLastError(tr("Unable to create file: %1").arg(exportFilename));
        return false;
    }

    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    stream << csvText;
    file.close();

    _setLastError(QString());
    return true;
}

void ThrusterMappingExportController::_setLastError(const QString &error)
{
    if (_lastError == error) {
        return;
    }

    _lastError = error;
    emit lastErrorChanged();
}

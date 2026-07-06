/****************************************************************************
 *
 * DeepShark AUV workspace settings and mission preview controller.
 *
 ****************************************************************************/

#include "DeepSharkAuvController.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QSettings>
#include <QtCore/QtMath>

namespace {
constexpr const char *kSettingsGroup = "DeepShark/Auv";
constexpr const char *kDebugMode = "DebugMode";
constexpr const char *kMapLatitude = "MapLatitude";
constexpr const char *kMapLongitude = "MapLongitude";
constexpr const char *kMapZoom = "MapZoom";
constexpr const char *kDefaultDepth = "DefaultDepth";
constexpr const char *kDefaultSpeed = "DefaultSpeed";
constexpr const char *kFollowVehicle = "FollowVehicle";
constexpr const char *kShowTrail = "ShowTrail";
constexpr const char *kPreferSatellite = "PreferSatellite";
constexpr double kHuairouLatitude = 40.3068;
constexpr double kHuairouLongitude = 116.6116;
}

DeepSharkAuvController::DeepSharkAuvController(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    _debugMode = settings.value(kDebugMode, false).toBool();
    _mapLatitude = settings.value(kMapLatitude, kHuairouLatitude).toDouble();
    _mapLongitude = settings.value(kMapLongitude, kHuairouLongitude).toDouble();
    _mapZoom = settings.value(kMapZoom, 16.4).toDouble();
    _defaultDepth = settings.value(kDefaultDepth, 8.0).toDouble();
    _defaultSpeed = settings.value(kDefaultSpeed, 1.0).toDouble();
    _followVehicle = settings.value(kFollowVehicle, false).toBool();
    _showTrail = settings.value(kShowTrail, true).toBool();
    _preferSatellite = settings.value(kPreferSatellite, true).toBool();
    settings.endGroup();
    loadTestMission();
}

void DeepSharkAuvController::setDebugMode(bool value)
{
    if (_debugMode == value) {
        return;
    }
    _debugMode = value;
    _writeValue(kDebugMode, value);
    emit debugModeChanged();
}

void DeepSharkAuvController::setMapLatitude(double value)
{
    if (qFuzzyCompare(_mapLatitude, value)) {
        return;
    }
    _mapLatitude = value;
    _writeValue(kMapLatitude, value);
    emit mapLatitudeChanged();
}

void DeepSharkAuvController::setMapLongitude(double value)
{
    if (qFuzzyCompare(_mapLongitude, value)) {
        return;
    }
    _mapLongitude = value;
    _writeValue(kMapLongitude, value);
    emit mapLongitudeChanged();
}

void DeepSharkAuvController::setMapZoom(double value)
{
    value = qBound(2.0, value, 20.0);
    if (qFuzzyCompare(_mapZoom, value)) {
        return;
    }
    _mapZoom = value;
    _writeValue(kMapZoom, value);
    emit mapZoomChanged();
}

void DeepSharkAuvController::setDefaultDepth(double value)
{
    value = qMax(0.0, value);
    if (qFuzzyCompare(_defaultDepth, value)) {
        return;
    }
    _defaultDepth = value;
    _writeValue(kDefaultDepth, value);
    emit defaultDepthChanged();
}

void DeepSharkAuvController::setDefaultSpeed(double value)
{
    value = qMax(0.0, value);
    if (qFuzzyCompare(_defaultSpeed, value)) {
        return;
    }
    _defaultSpeed = value;
    _writeValue(kDefaultSpeed, value);
    emit defaultSpeedChanged();
}

void DeepSharkAuvController::setFollowVehicle(bool value)
{
    if (_followVehicle == value) {
        return;
    }
    _followVehicle = value;
    _writeValue(kFollowVehicle, value);
    emit followVehicleChanged();
}

void DeepSharkAuvController::setShowTrail(bool value)
{
    if (_showTrail == value) {
        return;
    }
    _showTrail = value;
    _writeValue(kShowTrail, value);
    emit showTrailChanged();
}

void DeepSharkAuvController::setPreferSatellite(bool value)
{
    if (_preferSatellite == value) {
        return;
    }
    _preferSatellite = value;
    _writeValue(kPreferSatellite, value);
    emit preferSatelliteChanged();
}

bool DeepSharkAuvController::importPlan(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        _setMissionError(tr("无法打开任务文件：%1").arg(file.errorString()));
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        _setMissionError(tr("任务文件不是有效的 JSON：%1").arg(parseError.errorString()));
        return false;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("fileType")).toString() != QStringLiteral("Plan")) {
        _setMissionError(tr("所选文件不是 QGroundControl Plan 任务文件"));
        return false;
    }

    const QJsonArray items = root.value(QStringLiteral("mission")).toObject().value(QStringLiteral("items")).toArray();
    QVariantList parsedWaypoints;
    double distance = 0.0;
    double previousLatitude = 0.0;
    double previousLongitude = 0.0;
    bool havePrevious = false;

    for (const QJsonValue &itemValue : items) {
        const QJsonObject item = itemValue.toObject();
        const QJsonArray coordinate = item.value(QStringLiteral("coordinate")).toArray();
        if (coordinate.size() < 2 || !coordinate.at(0).isDouble() || !coordinate.at(1).isDouble()) {
            continue;
        }

        const double latitude = coordinate.at(0).toDouble();
        const double longitude = coordinate.at(1).toDouble();
        if (qFuzzyIsNull(latitude) && qFuzzyIsNull(longitude)) {
            continue;
        }

        const double altitude = coordinate.size() > 2 && coordinate.at(2).isDouble()
                                    ? coordinate.at(2).toDouble()
                                    : _defaultDepth;
        QVariantMap waypoint;
        waypoint.insert(QStringLiteral("sequence"), parsedWaypoints.count() + 1);
        waypoint.insert(QStringLiteral("latitude"), latitude);
        waypoint.insert(QStringLiteral("longitude"), longitude);
        waypoint.insert(QStringLiteral("depth"), qAbs(altitude));
        waypoint.insert(QStringLiteral("command"), item.value(QStringLiteral("command")).toInt());
        parsedWaypoints.append(waypoint);

        if (havePrevious) {
            distance += _distanceMeters(previousLatitude, previousLongitude, latitude, longitude);
        }
        previousLatitude = latitude;
        previousLongitude = longitude;
        havePrevious = true;
    }

    if (parsedWaypoints.isEmpty()) {
        _setMissionError(tr("任务文件中没有可显示的经纬度航点"));
        return false;
    }

    _missionFile = filePath;
    _missionName = QFileInfo(filePath).completeBaseName();
    _waypoints = parsedWaypoints;
    _totalDistance = distance;
    _missionError.clear();
    _missionSummary = tr("%1 个航点，预计航程 %2 km")
                          .arg(_waypoints.count())
                          .arg(_totalDistance / 1000.0, 0, 'f', 2);
    emit missionChanged();
    return true;
}

void DeepSharkAuvController::clearMission()
{
    _missionFile.clear();
    _missionName.clear();
    _missionSummary.clear();
    _missionError.clear();
    _waypoints.clear();
    _totalDistance = 0.0;
    emit missionChanged();
}

void DeepSharkAuvController::loadTestMission()
{
    struct TestPoint {
        double latitude;
        double longitude;
        double depth;
    };

    // Short shore-launched route along the water side of Huairou Reservoir's main dam.
    const TestPoint testPoints[] = {
        {40.30570, 116.61075, 0.5},
        {40.30650, 116.61120, 3.0},
        {40.30720, 116.61220, 4.0},
        {40.30750, 116.61260, 4.0},
        {40.30725, 116.61285, 4.0},
        {40.30655, 116.61185, 3.5},
        {40.30595, 116.61100, 2.5},
        {40.30570, 116.61075, 0.5},
    };

    QVariantList route;
    double distance = 0.0;
    for (qsizetype index = 0; index < std::size(testPoints); ++index) {
        QVariantMap waypoint;
        waypoint.insert(QStringLiteral("sequence"), index + 1);
        waypoint.insert(QStringLiteral("latitude"), testPoints[index].latitude);
        waypoint.insert(QStringLiteral("longitude"), testPoints[index].longitude);
        waypoint.insert(QStringLiteral("depth"), testPoints[index].depth);
        waypoint.insert(QStringLiteral("command"), 16);
        route.append(waypoint);
        if (index > 0) {
            distance += _distanceMeters(testPoints[index - 1].latitude,
                                        testPoints[index - 1].longitude,
                                        testPoints[index].latitude,
                                        testPoints[index].longitude);
        }
    }

    _missionFile.clear();
    _missionName = tr("怀柔水库主坝近岸巡检任务");
    _waypoints = route;
    _totalDistance = distance;
    _missionError.clear();
    _missionSummary = tr("%1 个航点，岸基往返 %2 m")
                          .arg(_waypoints.count())
                          .arg(_totalDistance, 0, 'f', 0);
    emit missionChanged();
}

void DeepSharkAuvController::useHuairouReservoirPreset()
{
    setMapLatitude(kHuairouLatitude);
    setMapLongitude(kHuairouLongitude);
    setMapZoom(16.4);
}

void DeepSharkAuvController::resetSettings()
{
    setDebugMode(false);
    useHuairouReservoirPreset();
    setDefaultDepth(8.0);
    setDefaultSpeed(1.0);
    setFollowVehicle(false);
    setShowTrail(true);
    setPreferSatellite(true);
}

void DeepSharkAuvController::_writeValue(const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(key, value);
    settings.endGroup();
}

void DeepSharkAuvController::_setMissionError(const QString &error)
{
    _missionError = error;
    emit missionChanged();
}

double DeepSharkAuvController::_distanceMeters(double latitude1, double longitude1, double latitude2, double longitude2)
{
    constexpr double earthRadius = 6371000.0;
    const double latitudeDelta = qDegreesToRadians(latitude2 - latitude1);
    const double longitudeDelta = qDegreesToRadians(longitude2 - longitude1);
    const double a = qPow(qSin(latitudeDelta / 2.0), 2.0)
                     + qCos(qDegreesToRadians(latitude1)) * qCos(qDegreesToRadians(latitude2))
                           * qPow(qSin(longitudeDelta / 2.0), 2.0);
    return earthRadius * 2.0 * qAtan2(qSqrt(a), qSqrt(1.0 - a));
}

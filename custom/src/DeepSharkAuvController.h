/****************************************************************************
 *
 * DeepShark AUV workspace settings and mission preview controller.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>

class DeepSharkAuvController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool debugMode READ debugMode WRITE setDebugMode NOTIFY debugModeChanged)
    Q_PROPERTY(double mapLatitude READ mapLatitude WRITE setMapLatitude NOTIFY mapLatitudeChanged)
    Q_PROPERTY(double mapLongitude READ mapLongitude WRITE setMapLongitude NOTIFY mapLongitudeChanged)
    Q_PROPERTY(double mapZoom READ mapZoom WRITE setMapZoom NOTIFY mapZoomChanged)
    Q_PROPERTY(double defaultDepth READ defaultDepth WRITE setDefaultDepth NOTIFY defaultDepthChanged)
    Q_PROPERTY(double defaultSpeed READ defaultSpeed WRITE setDefaultSpeed NOTIFY defaultSpeedChanged)
    Q_PROPERTY(bool followVehicle READ followVehicle WRITE setFollowVehicle NOTIFY followVehicleChanged)
    Q_PROPERTY(bool showTrail READ showTrail WRITE setShowTrail NOTIFY showTrailChanged)
    Q_PROPERTY(bool preferSatellite READ preferSatellite WRITE setPreferSatellite NOTIFY preferSatelliteChanged)
    Q_PROPERTY(QString missionFile READ missionFile NOTIFY missionChanged)
    Q_PROPERTY(QString missionName READ missionName NOTIFY missionChanged)
    Q_PROPERTY(QString missionSummary READ missionSummary NOTIFY missionChanged)
    Q_PROPERTY(QString missionError READ missionError NOTIFY missionChanged)
    Q_PROPERTY(int waypointCount READ waypointCount NOTIFY missionChanged)
    Q_PROPERTY(double totalDistance READ totalDistance NOTIFY missionChanged)
    Q_PROPERTY(QVariantList waypoints READ waypoints NOTIFY missionChanged)

public:
    explicit DeepSharkAuvController(QObject *parent = nullptr);

    bool debugMode() const { return _debugMode; }
    double mapLatitude() const { return _mapLatitude; }
    double mapLongitude() const { return _mapLongitude; }
    double mapZoom() const { return _mapZoom; }
    double defaultDepth() const { return _defaultDepth; }
    double defaultSpeed() const { return _defaultSpeed; }
    bool followVehicle() const { return _followVehicle; }
    bool showTrail() const { return _showTrail; }
    bool preferSatellite() const { return _preferSatellite; }
    QString missionFile() const { return _missionFile; }
    QString missionName() const { return _missionName; }
    QString missionSummary() const { return _missionSummary; }
    QString missionError() const { return _missionError; }
    int waypointCount() const { return _waypoints.count(); }
    double totalDistance() const { return _totalDistance; }
    QVariantList waypoints() const { return _waypoints; }

    void setDebugMode(bool value);
    void setMapLatitude(double value);
    void setMapLongitude(double value);
    void setMapZoom(double value);
    void setDefaultDepth(double value);
    void setDefaultSpeed(double value);
    void setFollowVehicle(bool value);
    void setShowTrail(bool value);
    void setPreferSatellite(bool value);

    Q_INVOKABLE bool importPlan(const QString &filePath);
    Q_INVOKABLE void loadTestMission();
    Q_INVOKABLE void clearMission();
    Q_INVOKABLE void useHuairouReservoirPreset();
    Q_INVOKABLE void resetSettings();

signals:
    void debugModeChanged();
    void mapLatitudeChanged();
    void mapLongitudeChanged();
    void mapZoomChanged();
    void defaultDepthChanged();
    void defaultSpeedChanged();
    void followVehicleChanged();
    void showTrailChanged();
    void preferSatelliteChanged();
    void missionChanged();

private:
    void _writeValue(const QString &key, const QVariant &value);
    void _setMissionError(const QString &error);
    static double _distanceMeters(double latitude1, double longitude1, double latitude2, double longitude2);

    bool _debugMode = false;
    double _mapLatitude = 40.3068;
    double _mapLongitude = 116.6116;
    double _mapZoom = 16.4;
    double _defaultDepth = 8.0;
    double _defaultSpeed = 1.0;
    bool _followVehicle = false;
    bool _showTrail = true;
    bool _preferSatellite = true;
    QString _missionFile;
    QString _missionName;
    QString _missionSummary;
    QString _missionError;
    QVariantList _waypoints;
    double _totalDistance = 0.0;
};

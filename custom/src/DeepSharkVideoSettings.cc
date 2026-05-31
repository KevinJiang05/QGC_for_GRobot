/****************************************************************************
 *
 * DeepShark video settings stored in QSettings.
 *
 ****************************************************************************/

#include "DeepSharkVideoSettings.h"

#include <QtCore/QSettings>

namespace {
constexpr const char *kSettingsGroup = "DeepShark/Video";
constexpr const char *kCamera1Name = "Camera1Name";
constexpr const char *kCamera1Url = "Camera1Url";
constexpr const char *kCamera2Name = "Camera2Name";
constexpr const char *kCamera2Url = "Camera2Url";
constexpr const char *kCamera3Name = "Camera3Name";
constexpr const char *kCamera3Url = "Camera3Url";
constexpr const char *kCamera4Name = "Camera4Name";
constexpr const char *kCamera4Url = "Camera4Url";
}

DeepSharkVideoSettings::DeepSharkVideoSettings(QObject *parent)
    : QObject(parent)
    , _camera1Name(_readValue(kCamera1Name, QStringLiteral("前视")))
    , _camera1Url(_readValue(kCamera1Url, QString()))
    , _camera2Name(_readValue(kCamera2Name, QStringLiteral("左视")))
    , _camera2Url(_readValue(kCamera2Url, QString()))
    , _camera3Name(_readValue(kCamera3Name, QStringLiteral("右视")))
    , _camera3Url(_readValue(kCamera3Url, QString()))
    , _camera4Name(_readValue(kCamera4Name, QStringLiteral("后视")))
    , _camera4Url(_readValue(kCamera4Url, QString()))
{
}

void DeepSharkVideoSettings::setCamera1Name(const QString &name)
{
    if (_camera1Name == name) {
        return;
    }
    _camera1Name = name;
    _writeValue(kCamera1Name, _camera1Name);
    emit camera1NameChanged();
}

void DeepSharkVideoSettings::setCamera1Url(const QString &url)
{
    if (_camera1Url == url) {
        return;
    }
    _camera1Url = url;
    _writeValue(kCamera1Url, _camera1Url);
    emit camera1UrlChanged();
}

void DeepSharkVideoSettings::setCamera2Name(const QString &name)
{
    if (_camera2Name == name) {
        return;
    }
    _camera2Name = name;
    _writeValue(kCamera2Name, _camera2Name);
    emit camera2NameChanged();
}

void DeepSharkVideoSettings::setCamera2Url(const QString &url)
{
    if (_camera2Url == url) {
        return;
    }
    _camera2Url = url;
    _writeValue(kCamera2Url, _camera2Url);
    emit camera2UrlChanged();
}

void DeepSharkVideoSettings::setCamera3Name(const QString &name)
{
    if (_camera3Name == name) {
        return;
    }
    _camera3Name = name;
    _writeValue(kCamera3Name, _camera3Name);
    emit camera3NameChanged();
}

void DeepSharkVideoSettings::setCamera3Url(const QString &url)
{
    if (_camera3Url == url) {
        return;
    }
    _camera3Url = url;
    _writeValue(kCamera3Url, _camera3Url);
    emit camera3UrlChanged();
}

void DeepSharkVideoSettings::setCamera4Name(const QString &name)
{
    if (_camera4Name == name) {
        return;
    }
    _camera4Name = name;
    _writeValue(kCamera4Name, _camera4Name);
    emit camera4NameChanged();
}

void DeepSharkVideoSettings::setCamera4Url(const QString &url)
{
    if (_camera4Url == url) {
        return;
    }
    _camera4Url = url;
    _writeValue(kCamera4Url, _camera4Url);
    emit camera4UrlChanged();
}

void DeepSharkVideoSettings::setCamera(int index, const QString &name, const QString &url)
{
    switch (index) {
    case 1:
        setCamera1Name(name);
        setCamera1Url(url);
        break;
    case 2:
        setCamera2Name(name);
        setCamera2Url(url);
        break;
    case 3:
        setCamera3Name(name);
        setCamera3Url(url);
        break;
    case 4:
        setCamera4Name(name);
        setCamera4Url(url);
        break;
    default:
        break;
    }
}

QString DeepSharkVideoSettings::_readValue(const QString &key, const QString &defaultValue) const
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    const QString value = settings.value(key, defaultValue).toString();
    settings.endGroup();
    return value;
}

void DeepSharkVideoSettings::_writeValue(const QString &key, const QString &value)
{
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kSettingsGroup));
    settings.setValue(key, value);
    settings.endGroup();
}

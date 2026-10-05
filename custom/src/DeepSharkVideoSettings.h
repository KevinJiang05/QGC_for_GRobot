/****************************************************************************
 *
 * DeepShark video settings stored in QSettings.
 *
 ****************************************************************************/

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class DeepSharkVideoSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int rtspTransport READ rtspTransport WRITE setRtspTransport NOTIFY rtspTransportChanged)
    Q_PROPERTY(QString camera1Name READ camera1Name WRITE setCamera1Name NOTIFY camera1NameChanged)
    Q_PROPERTY(QString camera1Url READ camera1Url WRITE setCamera1Url NOTIFY camera1UrlChanged)
    Q_PROPERTY(bool camera1Enabled READ camera1Enabled WRITE setCamera1Enabled NOTIFY camera1EnabledChanged)
    Q_PROPERTY(QString camera2Name READ camera2Name WRITE setCamera2Name NOTIFY camera2NameChanged)
    Q_PROPERTY(QString camera2Url READ camera2Url WRITE setCamera2Url NOTIFY camera2UrlChanged)
    Q_PROPERTY(bool camera2Enabled READ camera2Enabled WRITE setCamera2Enabled NOTIFY camera2EnabledChanged)
    Q_PROPERTY(QString camera3Name READ camera3Name WRITE setCamera3Name NOTIFY camera3NameChanged)
    Q_PROPERTY(QString camera3Url READ camera3Url WRITE setCamera3Url NOTIFY camera3UrlChanged)
    Q_PROPERTY(bool camera3Enabled READ camera3Enabled WRITE setCamera3Enabled NOTIFY camera3EnabledChanged)
    Q_PROPERTY(QString camera4Name READ camera4Name WRITE setCamera4Name NOTIFY camera4NameChanged)
    Q_PROPERTY(QString camera4Url READ camera4Url WRITE setCamera4Url NOTIFY camera4UrlChanged)
    Q_PROPERTY(bool camera4Enabled READ camera4Enabled WRITE setCamera4Enabled NOTIFY camera4EnabledChanged)

public:
    enum RtspTransport { Automatic = 0, Tcp, Udp };
    Q_ENUM(RtspTransport)
    explicit DeepSharkVideoSettings(QObject *parent = nullptr);

    int rtspTransport() const { return _rtspTransport; }
    void setRtspTransport(int transport);
    Q_INVOKABLE QString streamUrl(const QString &url, int transport) const;

    QString camera1Name() const { return _camera1Name; }
    QString camera1Url() const { return _camera1Url; }
    bool camera1Enabled() const { return _camera1Enabled; }
    QString camera2Name() const { return _camera2Name; }
    QString camera2Url() const { return _camera2Url; }
    bool camera2Enabled() const { return _camera2Enabled; }
    QString camera3Name() const { return _camera3Name; }
    QString camera3Url() const { return _camera3Url; }
    bool camera3Enabled() const { return _camera3Enabled; }
    QString camera4Name() const { return _camera4Name; }
    QString camera4Url() const { return _camera4Url; }
    bool camera4Enabled() const { return _camera4Enabled; }

    void setCamera1Name(const QString &name);
    void setCamera1Url(const QString &url);
    void setCamera1Enabled(bool enabled);
    void setCamera2Name(const QString &name);
    void setCamera2Url(const QString &url);
    void setCamera2Enabled(bool enabled);
    void setCamera3Name(const QString &name);
    void setCamera3Url(const QString &url);
    void setCamera3Enabled(bool enabled);
    void setCamera4Name(const QString &name);
    void setCamera4Url(const QString &url);
    void setCamera4Enabled(bool enabled);

    Q_INVOKABLE void setCamera(int index, const QString &name, const QString &url);
    Q_INVOKABLE void setCameraEnabled(int index, bool enabled);

signals:
    void rtspTransportChanged();
    void camera1NameChanged();
    void camera1UrlChanged();
    void camera1EnabledChanged();
    void camera2NameChanged();
    void camera2UrlChanged();
    void camera2EnabledChanged();
    void camera3NameChanged();
    void camera3UrlChanged();
    void camera3EnabledChanged();
    void camera4NameChanged();
    void camera4UrlChanged();
    void camera4EnabledChanged();

private:
    int _rtspTransport = Automatic;
    QString _readValue(const QString &key, const QString &defaultValue) const;
    void _writeValue(const QString &key, const QString &value);
    bool _readBool(const QString &key, bool defaultValue) const;
    void _writeBool(const QString &key, bool value);

    QString _camera1Name;
    QString _camera1Url;
    bool _camera1Enabled = true;
    QString _camera2Name;
    QString _camera2Url;
    bool _camera2Enabled = true;
    QString _camera3Name;
    QString _camera3Url;
    bool _camera3Enabled = true;
    QString _camera4Name;
    QString _camera4Url;
    bool _camera4Enabled = true;
};

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
    Q_PROPERTY(QString camera1Name READ camera1Name WRITE setCamera1Name NOTIFY camera1NameChanged)
    Q_PROPERTY(QString camera1Url READ camera1Url WRITE setCamera1Url NOTIFY camera1UrlChanged)
    Q_PROPERTY(QString camera2Name READ camera2Name WRITE setCamera2Name NOTIFY camera2NameChanged)
    Q_PROPERTY(QString camera2Url READ camera2Url WRITE setCamera2Url NOTIFY camera2UrlChanged)
    Q_PROPERTY(QString camera3Name READ camera3Name WRITE setCamera3Name NOTIFY camera3NameChanged)
    Q_PROPERTY(QString camera3Url READ camera3Url WRITE setCamera3Url NOTIFY camera3UrlChanged)
    Q_PROPERTY(QString camera4Name READ camera4Name WRITE setCamera4Name NOTIFY camera4NameChanged)
    Q_PROPERTY(QString camera4Url READ camera4Url WRITE setCamera4Url NOTIFY camera4UrlChanged)

public:
    explicit DeepSharkVideoSettings(QObject *parent = nullptr);

    QString camera1Name() const { return _camera1Name; }
    QString camera1Url() const { return _camera1Url; }
    QString camera2Name() const { return _camera2Name; }
    QString camera2Url() const { return _camera2Url; }
    QString camera3Name() const { return _camera3Name; }
    QString camera3Url() const { return _camera3Url; }
    QString camera4Name() const { return _camera4Name; }
    QString camera4Url() const { return _camera4Url; }

    void setCamera1Name(const QString &name);
    void setCamera1Url(const QString &url);
    void setCamera2Name(const QString &name);
    void setCamera2Url(const QString &url);
    void setCamera3Name(const QString &name);
    void setCamera3Url(const QString &url);
    void setCamera4Name(const QString &name);
    void setCamera4Url(const QString &url);

    Q_INVOKABLE void setCamera(int index, const QString &name, const QString &url);

signals:
    void camera1NameChanged();
    void camera1UrlChanged();
    void camera2NameChanged();
    void camera2UrlChanged();
    void camera3NameChanged();
    void camera3UrlChanged();
    void camera4NameChanged();
    void camera4UrlChanged();

private:
    QString _readValue(const QString &key, const QString &defaultValue) const;
    void _writeValue(const QString &key, const QString &value);

    QString _camera1Name;
    QString _camera1Url;
    QString _camera2Name;
    QString _camera2Url;
    QString _camera3Name;
    QString _camera3Url;
    QString _camera4Name;
    QString _camera4Url;
};

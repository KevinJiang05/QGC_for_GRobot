/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "QGCCameraManagerTest.h"
#include "QGCCameraManager.h"
#include "Vehicle.h"

#include <QtTest/QTest>
#include <QtTest/QSignalSpy>
#include <memory>

void QGCCameraManagerTest::_testCameraList()
{
    const QList<CameraMetaData*> cameraList = QGCCameraManager::_parseCameraMetaData(QStringLiteral(":/json/CameraMetaData.json"));

    QVERIFY(!cameraList.isEmpty());

    qDeleteAll(cameraList);
}

void QGCCameraManagerTest::_lateCameraReplyAfterManagerDestroyed()
{
    _connectMockLink(MAV_AUTOPILOT_INVALID);
    QVERIFY(_vehicle);
    for (int retry = 0; retry < 2; ++retry) {
        auto manager = std::make_unique<QGCCameraManager>(_vehicle);
        const int component = MAV_COMP_ID_CAMERA + retry;
        auto info = new QGCCameraManager::CameraStruct(manager.get(), component, _vehicle);
        info->retryCount = retry; // Exercise both request-message and legacy command callbacks.
        QSignalSpy destroyed(info, &QObject::destroyed);
        manager->_requestCameraInfo(info);
        manager.reset();
        QCOMPARE(destroyed.count(), 1);

        QSignalSpy received(_vehicle, &Vehicle::mavlinkMessageReceived);
        mavlink_message_t ack{};
        mavlink_msg_command_ack_pack(_vehicle->id(), component, &ack,
                                     retry == 0 ? MAV_CMD_REQUEST_MESSAGE : MAV_CMD_REQUEST_CAMERA_INFORMATION,
                                     MAV_RESULT_DENIED, 0, 0, 0, 0);
        _mockLink->respondWithMavlinkMessage(ack);
        QTRY_VERIFY_WITH_TIMEOUT(received.count() > 0, 1000);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    _disconnectMockLink();
}

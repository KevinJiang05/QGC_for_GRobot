#include "FlyViewCameraCaptureUITest.h"

#include <QtCore/QDir>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include "MavlinkCameraControlInterface.h"
#include "MockLink.h"
#include "QGCCameraManager.h"
#include "Vehicle.h"

UT_REGISTER_TEST(FlyViewCameraCaptureUITest, TestLabel::Integration)

void FlyViewCameraCaptureUITest::_testCaptureLayout_data()
{
    QTest::addColumn<bool>("photoMode");
    QTest::newRow("photo") << true;
    QTest::newRow("video") << false;
}

void FlyViewCameraCaptureUITest::_testCaptureLayout()
{
    QFETCH(bool, photoMode);
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(MockConfiguration::OptionEnableCamera); },
        [this, photoMode](QPointer<MockLink>, Vehicle* vehicle) {
            auto* const manager = vehicle->cameraManager();
            QVERIFY(manager);
            MavlinkCameraControlInterface* camera = nullptr;
            const auto findCamera = [&]() {
                for (int i = 0; i < manager->cameras()->count(); ++i) {
                    auto* candidate = qobject_cast<MavlinkCameraControlInterface*>(manager->cameras()->get(i));
                    if (candidate && candidate->compID() == MAV_COMP_ID_CAMERA) {
                        camera = candidate;
                        return true;
                    }
                }
                return false;
            };
            QVERIFY_TRUE_WAIT(findCamera(), TestTimeout::longMs());
            manager->setCurrentCamera(manager->cameras()->indexOf(camera));
            if (photoMode) {
                camera->setCameraModePhoto();
            } else {
                camera->setCameraModeVideo();
            }
            const auto mode = photoMode ? MavlinkCameraControlInterface::CAM_MODE_PHOTO
                                        : MavlinkCameraControlInterface::CAM_MODE_VIDEO;
            QVERIFY_TRUE_WAIT(camera->cameraMode() == mode, TestTimeout::mediumMs());
            auto* const capture =
                findVisibleItem(_rootItem, photoMode ? QStringLiteral("photoVideoControl_photoCaptureButton")
                                                     : QStringLiteral("photoVideoControl_videoCaptureButton"));
            auto* const counter =
                findVisibleItem(_rootItem, photoMode ? QStringLiteral("photoVideoControl_captureCount")
                                                     : QStringLiteral("photoVideoControl_recordTime"));
            auto* const settings = findVisibleItem(_rootItem, QStringLiteral("photoVideoControl_settingsButton"));
            QVERIFY(capture);
            QVERIFY(counter);
            QVERIFY(settings);
            const auto bounds = [](QQuickItem* item) {
                return QRectF(item->mapToScene(QPointF()), QSizeF(item->width(), item->height()));
            };
            const auto layoutDescription = [&]() {
                return QStringLiteral("Capture/counter/settings centre Y: %1/%2/%3; same row: %4/%5")
                    .arg(bounds(capture).center().y())
                    .arg(bounds(counter).center().y())
                    .arg(bounds(settings).center().y())
                    .arg(capture->parentItem() == counter->parentItem())
                    .arg(capture->parentItem() == settings->parentItem());
            };
            if (qApp->platformName() != QLatin1String("offscreen")) {
                QVERIFY(_window->grabWindow().save(QDir::current().filePath(
                    photoMode ? QStringLiteral("photo-layout.png") : QStringLiteral("video-layout.png"))));
            }
            QTRY_VERIFY2_WITH_TIMEOUT(qAbs(bounds(capture).center().y() - bounds(counter).center().y()) < 1.0,
                                      qPrintable(layoutDescription()), 3000);
            QTRY_VERIFY_WITH_TIMEOUT(qAbs(bounds(capture).center().y() - bounds(settings).center().y()) < 1.0, 3000);
            QVERIFY(bounds(capture).right() <= bounds(counter).left());
            QVERIFY(bounds(counter).right() <= bounds(settings).left());
            QVERIFY(clickItemFraction(QStringLiteral("photoVideoControl_settingsButton"), 0.5, 0.5));
            QVERIFY(waitForDialog(QCoreApplication::translate("PhotoVideoControl", "Settings")));
        });
}

void FlyViewCameraCaptureUITest::_testTimelapseShutterStopsCapture()
{
    runWithMockLink(
        [] { return MockLink::startPX4MockLink(MockConfiguration::OptionEnableCamera); },
        [this](QPointer<MockLink> mockLink, Vehicle* vehicle) {
            QGCCameraManager* const cameraManager = vehicle->cameraManager();
            QVERIFY(cameraManager);

            auto findCamera = [cameraManager]() -> MavlinkCameraControlInterface* {
                for (int i = 0; i < cameraManager->cameras()->count(); i++) {
                    auto* const cam = qobject_cast<MavlinkCameraControlInterface*>(cameraManager->cameras()->get(i));
                    if (cam && (cam->compID() == MAV_COMP_ID_CAMERA)) {
                        return cam;
                    }
                }
                return nullptr;
            };
            MavlinkCameraControlInterface* camera = nullptr;
            QVERIFY_TRUE_WAIT((camera = findCamera()) != nullptr, TestTimeout::longMs());
            QVERIFY(camera->capturesPhotos());
            QVERIFY_TRUE_WAIT(camera->capturePhotosState() == MavlinkCameraControlInterface::CapturePhotosStateIdle,
                              TestTimeout::longMs());

            cameraManager->setCurrentCamera(cameraManager->cameras()->indexOf(camera));
            QCOMPARE(cameraManager->currentCameraInstance(), camera);

            // Mock camera 1 starts in video mode; the shutter button is only shown in photo mode
            camera->setCameraModePhoto();
            QVERIFY_TRUE_WAIT(camera->cameraMode() == MavlinkCameraControlInterface::CAM_MODE_PHOTO,
                              TestTimeout::mediumMs());

            camera->setPhotoCaptureMode(MavlinkCameraControlInterface::PHOTO_CAPTURE_TIMELAPSE);
            camera->setPhotoLapse(1.0);
            camera->setPhotoLapseCount(0);

            const QString buttonName = QStringLiteral("photoVideoControl_photoCaptureButton");
            QVERIFY2(findVisibleItem(_rootItem, buttonName, TestTimeout::mediumMs()),
                     "Photo capture button never became visible");

            auto startCount = [&mockLink] {
                return mockLink->receivedMavCommandCount(MAV_CMD_IMAGE_START_CAPTURE, MAV_COMP_ID_CAMERA);
            };
            auto stopCount = [&mockLink] {
                return mockLink->receivedMavCommandCount(MAV_CMD_IMAGE_STOP_CAPTURE, MAV_COMP_ID_CAMERA);
            };
            QCOMPARE(startCount(), 0);
            QCOMPARE(stopCount(), 0);

            QVERIFY(clickItemFraction(buttonName, 0.5, 0.5));
            QVERIFY_TRUE_WAIT(startCount() == 1, TestTimeout::mediumMs());
            // Camera broadcasts CAMERA_IMAGE_CAPTURED per interval shot, which drives the photo counter
            QVERIFY_TRUE_WAIT(vehicle->cameraTriggerPoints()->count() >= 1, TestTimeout::mediumMs());

            // Second click while the interval capture is running must stop it
            QVERIFY(clickItemFraction(buttonName, 0.5, 0.5));
            QVERIFY_TRUE_WAIT(stopCount() == 1, TestTimeout::mediumMs());
            QVERIFY_TRUE_WAIT(camera->capturePhotosState() == MavlinkCameraControlInterface::CapturePhotosStateIdle,
                              TestTimeout::mediumMs());
        });
}

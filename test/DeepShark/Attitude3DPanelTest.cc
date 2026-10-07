#include "Attitude3DPanelTest.h"

#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QTimer>
#include <QtCore/QtMath>
#include <QtGui/QImage>
#include <QtGui/QQuaternion>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QSGRendererInterface>
#include <QtTest/QSignalSpy>
#include <cmath>
#include <memory>

#include "ColoredSvgImageProvider.h"
#include "QGCCorePlugin.h"

namespace {
class PanelFixture
{
public:
    PanelFixture()
    {
        QGCCorePlugin::instance()->init();
        engine.addImportPath(QStringLiteral("qrc:/qml"));
        engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            QtObject {
                property QtObject roll: QtObject { property real rawValue: 0 }
                property QtObject pitch: QtObject { property real rawValue: 0 }
                property QtObject heading: QtObject { property real rawValue: 0 }
                property QtObject altitudeRelative: QtObject { property real rawValue: 0 }
                property QtObject vehicleLinkManager: QtObject { property bool communicationLost: false }
            }
        )",
                          QUrl());
        vehicle.reset(component.create());
    }

    void setAltitude(double altitude)
    {
        vehicle->property("altitudeRelative").value<QObject*>()->setProperty("rawValue", altitude);
    }

    void setAngles(double roll, double pitch, double heading)
    {
        const auto setFact = [this](const char* name, double value) {
            vehicle->property(name).value<QObject*>()->setProperty("rawValue", value);
        };
        setFact("roll", roll);
        setFact("pitch", pitch);
        setFact("heading", heading);
    }

    std::unique_ptr<QObject> createPanel(QObject* state = nullptr)
    {
        QQmlComponent component(
            &engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/Attitude3DPanel.qml")));
        QVariantMap properties{{QStringLiteral("vehicle"), QVariant::fromValue(vehicle.get())}};
        if (state) {
            properties.insert(QStringLiteral("viewState"), QVariant::fromValue(state));
        }
        auto panel = std::unique_ptr<QObject>(component.createWithInitialProperties(properties));
        error = component.errorString();
        return panel;
    }

    QQmlEngine engine;
    std::unique_ptr<QObject> vehicle;
    QString error;
};

bool sameRotation(const QQuaternion& actual, const QQuaternion& expected)
{
    return qAbs(QQuaternion::dotProduct(actual.normalized(), expected.normalized())) > 0.99999f;
}

double angularDistance(double a, double b)
{
    return qAbs(std::remainder(a - b, 360.0));
}

std::unique_ptr<QObject> createViewState(QQmlEngine* engine)
{
    QQmlComponent component(
        engine, QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/Attitude3DViewState.qml")));
    return std::unique_ptr<QObject>(component.create());
}

bool advanceFollow(QObject* state, double seconds, double yaw, double height = 0)
{
    return QMetaObject::invokeMethod(state, "advanceFollow", Q_ARG(QVariant, QVariant(seconds)),
                                     Q_ARG(QVariant, QVariant(yaw)), Q_ARG(QVariant, QVariant(height)),
                                     Q_ARG(QVariant, QVariant(30)),
                                     Q_ARG(QVariant, QVariant::fromValue<QObject*>(nullptr)));
}
}  // namespace

void Attitude3DPanelTest::_poseAndLevelCamera_data()
{
    QTest::addColumn<double>("roll");
    QTest::addColumn<double>("pitch");
    QTest::addColumn<double>("heading");
    QTest::newRow("level-north") << 0.0 << 0.0 << 0.0;
    QTest::newRow("right-roll-east") << 30.0 << 0.0 << 90.0;
    QTest::newRow("nose-up-south") << 0.0 << 25.0 << 180.0;
    QTest::newRow("combined-west") << -35.0 << -20.0 << 270.0;
    QTest::newRow("near-vertical") << 40.0 << 85.0 << 359.0;
}

void Attitude3DPanelTest::_poseAndLevelCamera()
{
    QFETCH(double, roll);
    QFETCH(double, pitch);
    QFETCH(double, heading);
    PanelFixture fixture;
    QVERIFY(fixture.vehicle);
    fixture.setAngles(roll, pitch, heading);
    auto panel = fixture.createPanel();
    QVERIFY2(panel, qPrintable(fixture.error));
    QObject* body = panel->findChild<QObject*>(QStringLiteral("attitudeBody"));
    QObject* camera = panel->findChild<QObject*>(QStringLiteral("attitudeCamera"));
    QVERIFY(body);
    QVERIFY(camera);
    const auto expectedBody = QQuaternion::fromEulerAngles(pitch, -heading, -roll);
    const auto expectedCamera = QQuaternion::fromEulerAngles(-18, -heading, 0);
    QTRY_VERIFY_WITH_TIMEOUT(sameRotation(body->property("sceneRotation").value<QQuaternion>(), expectedBody), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(sameRotation(camera->property("sceneRotation").value<QQuaternion>(), expectedCamera),
                             2000);
    // The camera's screen-right vector stays horizontal even when the vehicle tilts.
    QVERIFY(qAbs(camera->property("sceneRotation").value<QQuaternion>().rotatedVector(QVector3D(1, 0, 0)).y()) <
            0.001f);
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "selectViewMode", Q_ARG(QVariant, QVariant(7))));
    fixture.setAngles(roll, pitch, heading + 45);
    QTRY_VERIFY_WITH_TIMEOUT(sameRotation(camera->property("sceneRotation").value<QQuaternion>(),
                                          QQuaternion::fromEulerAngles(-18, -heading - 45, 0)),
                             2000);
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "selectViewMode", Q_ARG(QVariant, QVariant(1))));
    QTRY_VERIFY_WITH_TIMEOUT(
        sameRotation(camera->property("sceneRotation").value<QQuaternion>(), QQuaternion::fromEulerAngles(-18, -28, 0)),
        2000);
}

void Attitude3DPanelTest::_headingWrapUsesShortPath()
{
    PanelFixture fixture;
    fixture.setAngles(0, 0, 359);
    auto panel = fixture.createPanel();
    QVERIFY2(panel, qPrintable(fixture.error));
    QObject* headingNode = panel->findChild<QObject*>(QStringLiteral("attitudeHeading"));
    QVERIFY(headingNode);
    QTRY_VERIFY_WITH_TIMEOUT(
        sameRotation(headingNode->property("rotation").value<QQuaternion>(), QQuaternion::fromEulerAngles(0, -359, 0)),
        2000);
    const auto start = headingNode->property("rotation").value<QQuaternion>();
    // Observe every animation update, rather than only the final (equivalent) orientation.
    QSignalSpy changes(headingNode, SIGNAL(rotationChanged()));
    QVERIFY(changes.isValid());
    QQmlComponent observerComponent(&fixture.engine);
    observerComponent.setData(R"(
        import QtQuick
        QtObject {
            required property QtObject targetNode
            required property quaternion startRotation
            property bool departed: false
            property Connections watcher: Connections {
                target: targetNode
                function onRotationChanged() {
                    const q = targetNode.rotation
                    const s = startRotation
                    if (Math.abs(q.scalar * s.scalar + q.x * s.x + q.y * s.y + q.z * s.z) < 0.999) {
                        departed = true
                    }
                }
            }
        }
    )",
                              QUrl());
    std::unique_ptr<QObject> observer(
        observerComponent.createWithInitialProperties({{QStringLiteral("targetNode"), QVariant::fromValue(headingNode)},
                                                       {QStringLiteral("startRotation"), QVariant::fromValue(start)}}));
    QVERIFY2(observer, qPrintable(observerComponent.errorString()));
    fixture.setAngles(0, 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(
        sameRotation(headingNode->property("rotation").value<QQuaternion>(), QQuaternion::fromEulerAngles(0, -1, 0)),
        2000);
    QVERIFY(!observer->property("departed").toBool());
    QVERIFY(!changes.isEmpty());
}

void Attitude3DPanelTest::_viewStateSurvivesPanelRecreation()
{
    PanelFixture fixture;
    fixture.setAngles(0, 0, 123);
    QQmlComponent stateComponent(
        &fixture.engine,
        QUrl(QStringLiteral("qrc:/Custom/qml/QGroundControl/FlyView/DeepShark/Attitude3DViewState.qml")));
    std::unique_ptr<QObject> sharedState(stateComponent.create());
    QVERIFY2(sharedState, qPrintable(stateComponent.errorString()));
    QObject* state = sharedState.get();
    auto panel = fixture.createPanel(state);
    QVERIFY2(panel, qPrintable(fixture.error));
    QVERIFY(QMetaObject::invokeMethod(panel.get(), "selectViewMode", Q_ARG(QVariant, QVariant(4))));
    state->setProperty("cameraDistance", 720);
    auto enlarged = fixture.createPanel(state);
    QVERIFY2(enlarged, qPrintable(fixture.error));
    panel.reset();
    QCOMPARE(enlarged->property("viewState").value<QObject*>(), state);
    QCOMPARE(state->property("viewMode").toInt(), 4);
    QCOMPARE(state->property("cameraDistance").toDouble(), 720.0);
    QVERIFY(QMetaObject::invokeMethod(enlarged.get(), "selectViewMode", Q_ARG(QVariant, QVariant(6))));
    const double worldYaw = enlarged->property("cameraWorldYaw").toDouble();
    fixture.setAngles(0, 0, 240);
    QCOMPARE(enlarged->property("cameraWorldYaw").toDouble(), worldYaw);
    QVERIFY(QMetaObject::invokeMethod(enlarged.get(), "resetView"));
    QCOMPARE(state->property("viewMode").toInt(), 0);
    QCOMPARE(state->property("cameraDistance").toDouble(), 650.0);
}

void Attitude3DPanelTest::_missingAndLostTelemetry()
{
    PanelFixture fixture;
    fixture.setAngles(qQNaN(), 0, 0);
    auto panel = fixture.createPanel();
    QVERIFY2(panel, qPrintable(fixture.error));
    QCOMPARE(panel->property("attitudeValid").toBool(), false);
    QCOMPARE(panel->property("attitudeStatus").toString(), QStringLiteral("等待姿态数据"));
    fixture.setAngles(1, 2, 3);
    QCOMPARE(panel->property("attitudeValid").toBool(), true);
    fixture.vehicle->property("vehicleLinkManager").value<QObject*>()->setProperty("communicationLost", true);
    QCOMPARE(panel->property("attitudeStatus").toString(), QStringLiteral("通信中断"));
    panel->setProperty("vehicle", QVariant::fromValue<QObject*>(nullptr));
    QCOMPARE(panel->property("attitudeStatus").toString(), QStringLiteral("等待飞控连接"));
}

void Attitude3DPanelTest::_inertiaExposesTheSideAndSettles()
{
    QQmlEngine engine;
    auto state = createViewState(&engine);
    QVERIFY(state);
    QVERIFY(advanceFollow(state.get(), 0, 359));
    QVERIFY(advanceFollow(state.get(), 1.0 / 60, 1));
    // Crossing north must move only two degrees, rather than taking a full revolution.
    QVERIFY(qAbs(state->property("followYaw").toDouble() - 359) < 2);
    state->setProperty("followInitialized", false);
    QVERIFY(advanceFollow(state.get(), 0, 0));
    for (int frame = 1; frame <= 60; ++frame) {
        const double target = 90.0 * frame / 60;
        QVERIFY(advanceFollow(state.get(), 1.0 / 60, target));
        const double lag = target - state->property("followYaw").toDouble();
        QVERIFY(lag >= 0);
        QVERIFY(lag <= 30.001);
    }
    QVERIFY(90 - state->property("followYaw").toDouble() > 15);
    for (int frame = 0; frame < 60; ++frame) {
        QVERIFY(advanceFollow(state.get(), 1.0 / 60, 90));
    }
    QVERIFY(qAbs(state->property("followYaw").toDouble() - 90) < 1);
    // Reversal preserves the current angular momentum before smoothly changing direction.
    const double previousYaw = state->property("followYaw").toDouble();
    QVERIFY(advanceFollow(state.get(), 1.0 / 60, 85));
    QVERIFY(qAbs(state->property("followYaw").toDouble() - previousYaw) < 1);
}

void Attitude3DPanelTest::_springIsFrameRateIndependent()
{
    QQmlEngine engine;
    auto at30 = createViewState(&engine);
    auto at120 = createViewState(&engine);
    QVERIFY(at30);
    QVERIFY(at120);
    for (const auto& pair : {std::pair{at30.get(), 30}, std::pair{at120.get(), 120}}) {
        QVERIFY(advanceFollow(pair.first, 0, 0));
        for (int frame = 0; frame < pair.second; ++frame) {
            QVERIFY(advanceFollow(pair.first, 1.0 / pair.second, 15, -15));
        }
    }
    QVERIFY(qAbs(at30->property("followYaw").toDouble() - at120->property("followYaw").toDouble()) < 0.000001);
    QVERIFY(qAbs(at30->property("followHeight").toDouble() - at120->property("followHeight").toDouble()) < 0.000001);
}

void Attitude3DPanelTest::_depthUsesSurfaceZero()
{
    PanelFixture fixture;
    auto panel = fixture.createPanel();
    QVERIFY2(panel, qPrintable(fixture.error));
    QObject* state = panel->property("viewState").value<QObject*>();
    QObject* body = panel->findChild<QObject*>(QStringLiteral("attitudeHeading"));
    QVERIFY(state);
    QVERIFY(body);
    QCOMPARE(panel->property("depthMeters").toDouble(), 0.0);
    QCOMPARE(panel->property("depthScaleMaximum").toDouble(), 2.0);
    fixture.setAltitude(-1.5);
    QCOMPARE(panel->property("depthMeters").toDouble(), 1.5);
    QCOMPARE(state->property("vehicleY").toDouble(), -450.0);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(body->property("y").toDouble() + 450) < 0.05, 2000);
    // A positive altitude remains above the surface; do not silently take its absolute value.
    fixture.setAltitude(0.1);
    QCOMPARE(panel->property("depthMeters").toDouble(), -0.1);
    QCOMPARE(state->property("vehicleY").toDouble(), 30.0);
    fixture.setAltitude(qQNaN());
    QCOMPARE(panel->property("depthValid").toBool(), false);
    QVERIFY(qIsNaN(panel->property("depthMeters").toDouble()));
    QCOMPARE(state->property("vehicleY").toDouble(), 30.0);
    panel->setProperty("vehicle", QVariant::fromValue<QObject*>(nullptr));
    QCOMPARE(state->property("hasDepth").toBool(), false);
    QCOMPARE(state->property("vehicleY").toDouble(), 0.0);
}

void Attitude3DUITest::_previewWorkspaceRoundTrip()
{
    if (qEnvironmentVariable("QT_QUICK_BACKEND") == QStringLiteral("software")) {
        QSKIP("Qt Quick 3D requires an RHI backend; run this test with --onscreen.");
    }
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }
    _window->resize(1440, 900);
    QTRY_COMPARE_WITH_TIMEOUT(_rootItem->width(), 1440.0, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(_rootItem->height(), 900.0, 2000);
    QQuickItem* layer = findVisibleItem(_rootItem, QStringLiteral("deepSharkCustomLayer"), 5000);
    QVERIFY(layer);
    QQuickItem* preview = findVisibleItem(_rootItem, QStringLiteral("attitude3DPanel"), 5000);
    QVERIFY(preview);
    for (QQuickItem* ancestor = preview; ancestor; ancestor = ancestor->parentItem()) {
        ancestor->ensurePolished();
    }
    const QPointF position = preview->mapToScene(QPointF());
    layer->setProperty("statusPanelManualOpen", true);
    QCOMPARE(preview->mapToScene(QPointF()), position);
    QObject* state = preview->property("viewState").value<QObject*>();
    QVERIFY(state);
    QVERIFY(QMetaObject::invokeMethod(preview, "selectViewMode", Q_ARG(QVariant, QVariant(4))));
    QVERIFY(clickButton(QStringLiteral("expandAttitudePreview")));
    QQuickItem* workspace = findVisibleItem(_rootItem, QStringLiteral("deepSharkFourVideoPanel"), 5000);
    QVERIFY(workspace);
    QTRY_COMPARE_WITH_TIMEOUT(workspace->property("attitudeMode").toBool(), true, 2000);
    QQuickItem* enlarged = findVisibleItem(workspace, QStringLiteral("attitude3DPanel"), 5000);
    QVERIFY(enlarged);
    QCOMPARE(enlarged->property("viewState").value<QObject*>(), state);
    QCOMPARE(state->property("viewMode").toInt(), 4);
    if (QSGRendererInterface::isApiRhiBased(_window->rendererInterface()->graphicsApi())) {
        PanelFixture fixture;
        fixture.setAngles(22, -15, 135);
        fixture.setAltitude(-0.75);
        enlarged->setProperty("vehicle", QVariant::fromValue(fixture.vehicle.get()));
        QObject* body = enlarged->findChild<QObject*>(QStringLiteral("attitudeBody"));
        QVERIFY(body);
        QTRY_VERIFY_WITH_TIMEOUT(sameRotation(body->property("sceneRotation").value<QQuaternion>(),
                                              QQuaternion::fromEulerAngles(-15, -135, -22)),
                                 2000);
        QObject* camera = enlarged->findChild<QObject*>(QStringLiteral("attitudeCamera"));
        QObject* headingNode = enlarged->findChild<QObject*>(QStringLiteral("attitudeHeading"));
        QVERIFY(camera);
        QVERIFY(headingNode);
        for (const int mode : {0, 7, 2, 4, 1}) {
            QVERIFY(QMetaObject::invokeMethod(enlarged, "selectViewMode", Q_ARG(QVariant, QVariant(mode))));
            if (mode == 0) {
                QTRY_VERIFY_WITH_TIMEOUT(angularDistance(enlarged->property("cameraWorldYaw").toDouble(), -135) < 1,
                                         3000);
            }
            QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
            _window->update();
            QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
            const QImage screenshot = _window->grabWindow();
            QVERIFY(!screenshot.isNull());
            QVERIFY(screenshot.save(QDir::current().filePath(QStringLiteral("attitude3d-view-%1.png").arg(mode))));
        }
        QVERIFY(QMetaObject::invokeMethod(enlarged, "resetView"));
        QElapsedTimer turnElapsed;
        QTimer turnTimer;
        turnTimer.setInterval(16);
        connect(&turnTimer, &QTimer::timeout, &turnTimer, [&]() {
            const double progress = qMin(1.0, turnElapsed.elapsed() / 1000.0);
            fixture.setAngles(22, -15, 135 + 90 * progress);
            if (progress == 1.0) {
                turnTimer.stop();
            }
        });
        turnElapsed.start();
        turnTimer.start();
        QTRY_VERIFY_WITH_TIMEOUT(angularDistance(enlarged->property("cameraWorldYaw").toDouble(),
                                                 enlarged->property("renderedHeading").toDouble()) > 16,
                                 2000);
        QVERIFY(_window->grabWindow().save(QDir::current().filePath(QStringLiteral("attitude3d-inertial-turn.png"))));
        QVERIFY(qAbs(camera->property("sceneRotation").value<QQuaternion>().rotatedVector(QVector3D(1, 0, 0)).y()) <
                0.001f);
        QTRY_VERIFY_WITH_TIMEOUT(!turnTimer.isActive(), 2000);
        QTRY_VERIFY_WITH_TIMEOUT(angularDistance(enlarged->property("cameraWorldYaw").toDouble(), -225) < 1, 3000);
        QVERIFY(QMetaObject::invokeMethod(enlarged, "selectViewMode", Q_ARG(QVariant, QVariant(7))));
        fixture.setAngles(22, -15, 270);
        QTRY_VERIFY_WITH_TIMEOUT(sameRotation(camera->property("sceneRotation").value<QQuaternion>(),
                                              QQuaternion::fromEulerAngles(-18, -270, 0)),
                                 2000);
        for (const double depth : {0.0, 1.5}) {
            fixture.setAltitude(-depth);
            QTRY_VERIFY_WITH_TIMEOUT(qAbs(headingNode->property("y").toDouble() + depth * 300) < 0.05, 2000);
            QSignalSpy depthRendered(_window, &QQuickWindow::frameSwapped);
            _window->update();
            QTRY_VERIFY_WITH_TIMEOUT(!depthRendered.isEmpty(), 5000);
            QVERIFY(_window->grabWindow().save(
                QDir::current().filePath(QStringLiteral("attitude3d-depth-%1.png").arg(depth))));
        }
        fixture.setAltitude(-0.75);
        fixture.setAngles(35, -60, 135);
        QVERIFY(QMetaObject::invokeMethod(enlarged, "resetView"));
        QTRY_VERIFY_WITH_TIMEOUT(sameRotation(body->property("sceneRotation").value<QQuaternion>(),
                                              QQuaternion::fromEulerAngles(-60, -135, -35)),
                                 2000);
        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        QVERIFY(_window->grabWindow().save(QDir::current().filePath(QStringLiteral("attitude3d-steep-pitch.png"))));
        const QPoint center = enlarged->mapToScene(QPointF(enlarged->width() / 2, enlarged->height() / 2)).toPoint();
        QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, center);
        QTest::mouseMove(_window, center + QPoint(60, 20));
        QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, center + QPoint(60, 20));
        QTRY_COMPARE_WITH_TIMEOUT(state->property("viewMode").toInt(), 6, 2000);
        QTest::mouseDClick(_window, Qt::LeftButton, Qt::NoModifier, center);
        QTRY_COMPARE_WITH_TIMEOUT(state->property("viewMode").toInt(), 0, 2000);
        enlarged->setProperty("vehicle", QVariant::fromValue<QObject*>(nullptr));
    }
    QVERIFY(QMetaObject::invokeMethod(workspace, "showVideoWorkspace"));
    preview = findVisibleItem(_rootItem, QStringLiteral("attitude3DPanel"), 5000);
    QVERIFY(preview);
    QCOMPARE(preview->property("viewState").value<QObject*>(), state);
    QTRY_COMPARE_WITH_TIMEOUT(preview->mapToScene(QPointF()), position, 2000);
    if (QSGRendererInterface::isApiRhiBased(_window->rendererInterface()->graphicsApi())) {
        PanelFixture fixture;
        fixture.setAngles(22, -15, 135);
        fixture.setAltitude(-0.75);
        preview->setProperty("vehicle", QVariant::fromValue(fixture.vehicle.get()));
        QVERIFY(QMetaObject::invokeMethod(preview, "resetView"));
        QObject* body = preview->findChild<QObject*>(QStringLiteral("attitudeBody"));
        QVERIFY(body);
        QTRY_VERIFY_WITH_TIMEOUT(sameRotation(body->property("sceneRotation").value<QQuaternion>(),
                                              QQuaternion::fromEulerAngles(-15, -135, -22)),
                                 2000);
        QTRY_VERIFY_WITH_TIMEOUT(angularDistance(preview->property("cameraWorldYaw").toDouble(), -135) < 1, 3000);
        QSignalSpy rendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!rendered.isEmpty(), 5000);
        const QImage screenshot = _window->grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(QDir::current().filePath(QStringLiteral("attitude3d-preview.png"))));
        QVERIFY(QMetaObject::invokeMethod(workspace, "toggleStatusPanel"));
        QTRY_COMPARE_WITH_TIMEOUT(layer->property("statusPanelExpanded").toBool(), false, 2000);
        QVERIFY(preview->isVisible());
        QCOMPARE(preview->mapToScene(QPointF()), position);
        QSignalSpy compactRendered(_window, &QQuickWindow::frameSwapped);
        _window->update();
        QTRY_VERIFY_WITH_TIMEOUT(!compactRendered.isEmpty(), 5000);
        QVERIFY(_window->grabWindow().save(
            QDir::current().filePath(QStringLiteral("attitude3d-preview-compact-status.png"))));
        preview->setProperty("vehicle", QVariant::fromValue<QObject*>(nullptr));
    }
}

UT_REGISTER_TEST(Attitude3DPanelTest, TestLabel::Unit)
UT_REGISTER_TEST(Attitude3DUITest, TestLabel::Integration)

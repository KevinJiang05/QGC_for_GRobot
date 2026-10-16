#include "SetupAvailabilityTest.h"

#include <QtCore/QPointer>
#include <QtCore/QRegularExpression>
#include <QtCore/QScopeGuard>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <functional>
#include <memory>

#include "AppSettings.h"
#include "AutoPilotPlugin.h"
#include "ColoredSvgImageProvider.h"
#include "Fact.h"
#include "FactMetaData.h"
#include "FlyViewSettings.h"
#include "LinkConfiguration.h"
#include "LinkManager.h"
#include "MockLink.h"
#include "MultiVehicleManager.h"
#include "ParameterManager.h"
#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "QmlObjectListModel.h"
#include "SettingsManager.h"
#include "Vehicle.h"
#include "VehicleComponent.h"

void SetupAvailabilityTest::cleanup()
{
    if (_window) {
        // The open drawer is not part of link teardown in these scenarios.
        (void) QMetaObject::invokeMethod(_window, "closeIndicatorDrawer");
        (void) QTest::qWaitFor(
            [&]() { return !findVisibleItem(_rootItem, QStringLiteral("mainStatus_allowForceArm"), 0); }, 2000);
    }
    if (_link) {
        _link->disconnect();
        _link = nullptr;
        settleEventLoopForCleanup();
    }
    QmlUITestBase::cleanup();
}

void SetupAvailabilityTest::_startConnectedUi()
{
    ignoreLogMessage("qt.qml.propertyCache.append", QtWarningMsg,
                     QRegularExpression(QStringLiteral("Member enabled of the object QQuickPinchArea overrides")));
    startUI();
    if (QTest::currentTestFailed()) {
        return;
    }
    _link = MockLink::startAPMArduSubMockLink();
    QVERIFY(_link);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), 10000);
    auto* vehicle = MultiVehicleManager::instance()->activeVehicle();
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->parameterManager()->parametersReady(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->isInitialConnectComplete(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->parameterReadyVehicleAvailable(), 10000);
}

void SetupAvailabilityTest::_validationOwnership()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QGroundControl
import QGroundControl.Controls
import QGroundControl.FactControls
ApplicationWindow {
    id: mainWindow
    width: 1000; height: 800
    property QtObject globals: QtObject { property int validationErrorCount: 0 }
    readonly property int count: globals.validationErrorCount
    property bool validateFocused: false
    function allowViewSwitch(previous = 0) {
        if (validateFocused && activeFocusControl instanceof FactTextField) {
            activeFocusControl._onEditingFinished()
        }
        return globals.validationErrorCount <= previous
    }
    Component { id: fieldFactory; QGCTextField {} }
    property var field: fieldFactory.createObject(contentItem)
    property var other: fieldFactory.createObject(contentItem)
    Fact { id: firstFact }
    Fact { id: secondFact }
    FactTextField { id: factField; objectName: "factField"; fact: firstFact }
    QGCPopupDialog {
        id: dialog
        objectName: "dialog"
        destroyOnClose: false
        buttons: Dialog.Ok | Dialog.Cancel
        QGCTextField { id: dialogField; objectName: "dialogField" }
        FactTextField { id: focusedField; objectName: "focusedDialogField"; fact: firstFact }
    }
    function invalidate() { field.showValidationError("invalid"); other.showValidationError("other") }
    function destroyFirst() { field.destroy(); field = null }
    function clearOther() { other.clearValidationError(); other.clearValidationError() }
    function nonBlocking() { other.showValidationError("hint", undefined, false) }
    function invalidateFact() { factField.showValidationError("fact invalid") }
    function switchFact() { factField.fact = secondFact }
    function openDialog() { dialog.open() }
    function invalidateDialog() { dialogField.showValidationError("dialog invalid") }
    function closeDialog() { dialog.close() }
    function tryAccept() { dialog._accept() }
    function prepareFocusedError() {
        focusedField.text = "not-a-number"
        focusedField.forceActiveFocus()
        validateFocused = true
    }
    function fixFocusedError() { focusedField.text = "1"; focusedField._onEditingFinished() }
})",
                      QUrl(QStringLiteral("qrc:/setup-validation-test.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    window->show();
    auto invoke = [&](const char* method) { return QMetaObject::invokeMethod(root.get(), method); };
    QVERIFY(invoke("invalidate"));
    QCOMPARE(root->property("count").toInt(), 2);
    QPointer<QObject> field = root->property("field").value<QObject*>();
    QVERIFY(invoke("destroyFirst"));
    QTRY_VERIFY_WITH_TIMEOUT(field.isNull(), 1000);
    QCOMPARE(root->property("count").toInt(), 1);
    QVERIFY(invoke("clearOther"));
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("nonBlocking"));
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("clearOther"));
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("invalidateFact"));
    QCOMPARE(root->property("count").toInt(), 1);
    QVERIFY(invoke("switchFact"));
    QCOMPARE(root->property("count").toInt(), 0);

    // A dialog releases only its own fields; later destruction must not decrement twice.
    QVERIFY(invoke("invalidateFact"));
    QVERIFY(invoke("openDialog"));
    auto* dialog = root->findChild<QObject*>(QStringLiteral("dialog"));
    QVERIFY(dialog);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    QVERIFY(invoke("invalidateDialog"));
    QCOMPARE(root->property("count").toInt(), 2);
    QVERIFY(invoke("closeDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 1000);
    QCOMPARE(root->property("count").toInt(), 1);
    auto* factField = root->findChild<QObject*>(QStringLiteral("factField"));
    QVERIFY(factField);
    QVERIFY(QMetaObject::invokeMethod(factField, "clearValidationError"));
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("openDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    QVERIFY(invoke("invalidateDialog"));
    QCOMPARE(root->property("count").toInt(), 1);
    QVERIFY(invoke("closeDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 1000);
    QCOMPARE(root->property("count").toInt(), 0);

    // Focus validation happens inside allowViewSwitch, so the dialog must check its fields afterwards.
    QSignalSpy focusedAccepted(dialog, SIGNAL(accepted()));
    QVERIFY(focusedAccepted.isValid());
    QVERIFY(invoke("invalidateFact"));
    QVERIFY(invoke("openDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    QVERIFY(QMetaObject::invokeMethod(factField, "clearValidationError"));
    QVERIFY(invoke("prepareFocusedError"));
    auto* focusedField = root->findChild<QQuickItem*>(QStringLiteral("focusedDialogField"));
    QVERIFY(focusedField);
    QTRY_VERIFY_WITH_TIMEOUT(focusedField->hasActiveFocus(), 1000);
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("tryAccept"));
    QCOMPARE(root->property("count").toInt(), 1);
    QCOMPARE(focusedAccepted.count(), 0);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(invoke("fixFocusedError"));
    QCOMPARE(root->property("count").toInt(), 0);
    QVERIFY(invoke("tryAccept"));
    QCOMPARE(focusedAccepted.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 1000);

    // A disappearing background error cannot make an invalid dialog acceptable.
    QVERIFY(invoke("invalidateFact"));
    QVERIFY(invoke("openDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    QVERIFY(invoke("invalidateDialog"));
    QCOMPARE(root->property("count").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(factField, "clearValidationError"));
    QCOMPARE(root->property("count").toInt(), 1);
    QSignalSpy accepted(dialog, SIGNAL(accepted()));
    QVERIFY(accepted.isValid());
    QVERIFY(invoke("tryAccept"));
    QCOMPARE(accepted.count(), 0);
    QVERIFY(dialog->property("visible").toBool());
    QVERIFY(invoke("closeDialog"));
    QTRY_VERIFY_WITH_TIMEOUT(!dialog->property("visible").toBool(), 1000);
    QCOMPARE(root->property("count").toInt(), 0);
}

void SetupAvailabilityTest::_linkEditorWaitsForSettingsPage()
{
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.AppSettings
ApplicationWindow {
    id: mainWindow
    width: 1000; height: 800
    property QtObject globals: QtObject { property int validationErrorCount: 0 }
    readonly property int configCount: QGroundControl.linkManager.linkConfigurations.count
    function allowViewSwitch(previous = 0) { return globals.validationErrorCount <= previous }
    LinkConfigurationManager { width: parent.width }
})",
                      QUrl(QStringLiteral("qrc:/link-editor-test.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    window->show();
    const int configCount = root->property("configCount").toInt();

    auto* addButton = root->findChild<QObject*>(QStringLiteral("linkConfig_addButton"));
    QVERIFY(addButton);
    QVERIFY(QMetaObject::invokeMethod(addButton, "clicked"));
    QPointer<QObject> dialog;
    QTRY_VERIFY_WITH_TIMEOUT(dialog = root->findChild<QObject*>(QStringLiteral("linkConfig_dialog")), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    auto* loader = dialog->findChild<QObject*>(QStringLiteral("linkConfig_settingsLoader"));
    auto* nameField = dialog->findChild<QObject*>(QStringLiteral("linkConfig_nameField"));
    QVERIFY(loader);
    QVERIFY(nameField);
    nameField->setProperty("text", QStringLiteral("SA23 link"));
    const QUrl settingsSource = loader->property("source").toUrl();
    QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject*>(), 5000);
    QVERIFY(dialog->property("acceptButtonEnabled").toBool());

    // A settings page that is not ready cannot be saved, by button or by a direct accept.
    expectLogMessage("qml", QtWarningMsg, QRegularExpression(QStringLiteral("Failed to load link settings page")));
    expectLogMessage("default", QtWarningMsg,
                     QRegularExpression(QStringLiteral("missing-link-settings\\.qml: No such file or directory")));
    loader->setProperty("source", QUrl(QStringLiteral("qrc:/missing-link-settings.qml")));
    // The old page goes at once; the asynchronous load reports its failure later (Loader.Error == 3).
    QTRY_COMPARE_WITH_TIMEOUT(loader->property("status").toInt(), 3, 2000);
    QVERIFY(!loader->property("item").value<QObject*>());
    verifyExpectedLogMessage();
    verifyExpectedLogMessage();
    QVERIFY(!dialog->property("acceptButtonEnabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(dialog, "_accept"));
    QVERIFY(dialog->property("visible").toBool());
    QCOMPARE(root->property("configCount").toInt(), configCount);

    loader->setProperty("source", settingsSource);
    QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject*>(), 5000);
    QVERIFY(dialog->property("acceptButtonEnabled").toBool());

    // Cancelling discards the new configuration instead of saving it.
    QVERIFY(QMetaObject::invokeMethod(dialog, "_reject"));
    // The dialog destroys itself on close; let that finish before the engine goes away.
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isNull(), 2000);
    QCOMPARE(root->property("configCount").toInt(), configCount);
}

void SetupAvailabilityTest::_linkEditorSavesExistingLink()
{
    // Saving an edited link deletes the edited copy; the serial page binds to it without null guards.
    auto* linkManager = LinkManager::instance();
    QPointer<LinkConfiguration> original =
        linkManager->createConfiguration(LinkConfiguration::TypeSerial, QStringLiteral("SA23 edit"));
    QVERIFY(original);
    // As the Add dialog does; dynamic configurations are not listed and cannot be edited.
    original->setDynamic(false);
    linkManager->endCreateConfiguration(original);
    const auto removeOriginal = qScopeGuard([&]() {
        if (original) {
            linkManager->removeConfiguration(original);
        }
    });

    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QtQuick.Controls
import QGroundControl
import QGroundControl.AppSettings
ApplicationWindow {
    id: mainWindow
    width: 1000; height: 800
    property QtObject globals: QtObject { property int validationErrorCount: 0 }
    function allowViewSwitch(previous = 0) { return globals.validationErrorCount <= previous }
    LinkConfigurationManager { width: parent.width }
})",
                      QUrl(QStringLiteral("qrc:/link-editor-save-test.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(window);
    window->show();
    QVERIFY(QTest::qWaitForWindowExposed(window));

    // Repeater delegates are not QObject children of the page, so search the visual item tree.
    QQuickItem* editButton = nullptr;
    const std::function<QQuickItem*(QQuickItem*)> findItem = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("linkConfig_edit_SA23 edit")) {
            return item;
        }
        for (QQuickItem* child : item->childItems()) {
            if (QQuickItem* match = findItem(child)) {
                return match;
            }
        }
        return nullptr;
    };
    const auto findEditButton = [&]() {
        editButton = findItem(window->contentItem());
        return editButton && editButton->isVisible() && editButton->width() > 0;
    };
    if (!QTest::qWaitFor(findEditButton, 2000)) {
        QStringList found;
        for (QObject* object : root->findChildren<QObject*>()) {
            if (object->objectName().startsWith(QStringLiteral("linkConfig_"))) {
                found.append(object->objectName());
            }
        }
        QStringList configs;
        const auto* model = linkManager->property("linkConfigurations").value<QmlObjectListModel*>();
        for (int i = 0; model && i < model->count(); ++i) {
            const auto* config = model->value<LinkConfiguration*>(i);
            configs.append(QStringLiteral("%1 dynamic=%2").arg(config->name()).arg(config->isDynamic()));
        }
        QFAIL(qPrintable(QStringLiteral("edit button not usable (found=%1, visible=%2, width=%3); objects=[%4]; "
                                        "configs=[%5]")
                             .arg(editButton != nullptr)
                             .arg(editButton && editButton->isVisible())
                             .arg(editButton ? editButton->width() : -1)
                             .arg(found.join(QStringLiteral(", ")), configs.join(QStringLiteral(", ")))));
    }
    const QPoint editPoint =
        editButton->mapToScene(QPointF(editButton->width() / 2, editButton->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, editPoint);

    QPointer<QObject> dialog;
    QTRY_VERIFY_WITH_TIMEOUT(dialog = root->findChild<QObject*>(QStringLiteral("linkConfig_dialog")), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(dialog->property("opened").toBool(), 1000);
    QPointer<QObject> editedCopy = dialog->property("editingConfig").value<QObject*>();
    QVERIFY(editedCopy);
    QVERIFY(editedCopy != original);
    auto* loader = dialog->findChild<QObject*>(QStringLiteral("linkConfig_settingsLoader"));
    auto* nameField = dialog->findChild<QObject*>(QStringLiteral("linkConfig_nameField"));
    QVERIFY(loader);
    QVERIFY(nameField);
    QTRY_VERIFY_WITH_TIMEOUT(loader->property("item").value<QObject*>(), 5000);
    QVERIFY(dialog->property("acceptButtonEnabled").toBool());

    nameField->setProperty("text", QStringLiteral("SA23 edited"));
    QVERIFY(QMetaObject::invokeMethod(dialog, "_accept"));
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isNull(), 2000);
    QVERIFY(editedCopy.isNull());
    QVERIFY(original);
    QCOMPARE(original->name(), QStringLiteral("SA23 edited"));
}

void SetupAvailabilityTest::_forceArmPermission_data()
{
    QTest::addColumn<bool>("secondIsSub");
    QTest::newRow("ArduSub-to-ArduSub") << true;
    QTest::newRow("ArduSub-to-ArduCopter") << false;
}

void SetupAvailabilityTest::_forceArmPermission()
{
    QFETCH(bool, secondIsSub);
    _startConnectedUi();
    if (QTest::currentTestFailed())
        return;
    const auto trace = [this](const QString& stage) {
        const QString message = QStringLiteral("SetupAvailability force-arm: ") + stage;
        expectLogMessage("default", QtInfoMsg,
                         QRegularExpression(QLatin1Char('^') + QRegularExpression::escape(message) + QLatin1Char('$')));
        qInfo().noquote() << message;
        verifyExpectedLogMessage();
    };
    auto* vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    // This scenario exercises force-arm, not parameter-metadata loading. Give the Mock's unbounded
    // timeout Fact a finite synthetic range before a ValueSlider turns that range into tick geometry.
    const auto boundMockTimeoutMetadata = [](Vehicle* mockVehicle) {
        Fact* timeout = mockVehicle->parameterManager()->getParameter(-1, QStringLiteral("FS_GCS_TIMEOUT"));
        if (!timeout || !timeout->metaData()) {
            return false;
        }
        if (!timeout->minIsDefaultForType() && !timeout->maxIsDefaultForType()) {
            return true;
        }
        auto* metadata = new FactMetaData(*timeout->metaData(), timeout);
        metadata->setRawMin(0.0f);
        metadata->setRawMax(60.0f);
        timeout->setMetaData(metadata);
        return !timeout->minIsDefaultForType() && !timeout->maxIsDefaultForType();
    };
    QVERIFY(boundMockTimeoutMetadata(vehicle));

    auto* status = findItem(_rootItem, QStringLiteral("toolbar_mainStatusIndicator"));
    QVERIFY(status);
    trace(QStringLiteral("opening drawer with bounded Mock metadata"));
    QVERIFY(QMetaObject::invokeMethod(status, "dropMainStatusIndicator"));
    QVERIFY(clickButton(QStringLiteral("indicatorDrawerExpandButton")));
    trace(QStringLiteral("drawer expanded"));
    QVERIFY(verifyChecked(QStringLiteral("mainStatus_allowForceArm"), false, QStringLiteral("initial")));
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(status->property("_allowForceArm").toBool());

    // An armed heartbeat must revoke permission even when no force-arm button was activated.
    trace(QStringLiteral("injecting armed heartbeat"));
    _link->setArmed(true);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), 3000);
    QVERIFY(!status->property("_allowForceArm").toBool());
    _link->setArmed(false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed(), 3000);
    QVERIFY(verifyChecked(QStringLiteral("mainStatus_allowForceArm"), false, QStringLiteral("after armed heartbeat")));
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(status->property("_allowForceArm").toBool());
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(!status->property("_allowForceArm").toBool());
    QVERIFY(!findVisibleItem(_rootItem, QStringLiteral("mainStatus_forceArmButton"), 0));
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(status->property("_allowForceArm").toBool());

    QPointer<QQuickItem> forceArmButton = findVisibleItem(_rootItem, QStringLiteral("mainStatus_forceArmButton"));
    QVERIFY(forceArmButton);
    QSignalSpy activated(forceArmButton, SIGNAL(activated()));
    QVERIFY(activated.isValid());
    _link->clearReceivedMavCommandCounts();
    trace(QStringLiteral("short press"));
    QVERIFY(clickButton(QStringLiteral("mainStatus_forceArmButton")));
    QVERIFY(waitForNoSignal(activated, 200, QStringLiteral("short force-arm press")));
    QCOMPARE(_link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 0);
    QVERIFY(status->property("_allowForceArm").toBool());

    // Exercise the real DelayButton timer and Vehicle -> MockLink command, not its activated handler directly.
    // A short release shows the hold-help text and reverses the style's progress animation. Wait for
    // those observable conditions and the same polished-position stability used by _clickItemAt().
    _window->requestActivate();
    QVERIFY2(QTest::qWaitForWindowActive(_window, 5000), "Native force-arm hold needs the active test window");
    QTRY_VERIFY_WITH_TIMEOUT(forceArmButton && !forceArmButton->property("pressed").toBool() &&
                                 qFuzzyIsNull(forceArmButton->property("progress").toDouble()),
                             2000);
    QCOMPARE(forceArmButton->window(), _window);
    QVERIFY(forceArmButton->isVisible());
    QVERIFY(forceArmButton->isEnabled());
    QPointF lastScenePosition(qQNaN(), qQNaN());
    QPointF scenePosition;
    QVERIFY(waitForCondition(
        [&]() {
            if (!forceArmButton) {
                return false;
            }
            for (QQuickItem* ancestor = forceArmButton; ancestor; ancestor = ancestor->parentItem()) {
                ancestor->ensurePolished();
            }
            scenePosition =
                forceArmButton->mapToScene(QPointF(forceArmButton->width() / 2, forceArmButton->height() / 2));
            const bool stable = scenePosition == lastScenePosition;
            lastScenePosition = scenePosition;
            return stable;
        },
        TestTimeout::shortMs(), QStringLiteral("force-arm hold position settled")));
    QVERIFY(forceArmButton);
    QVERIFY2(qIsFinite(scenePosition.x()) && qIsFinite(scenePosition.y()), "Force-arm press point must be finite");
    QVERIFY2(scenePosition.x() >= 0 && scenePosition.x() < _window->width() && scenePosition.y() >= 0 &&
                 scenePosition.y() < _window->height(),
             "Force-arm press point must be inside the test window");
    const QPoint pressPosition(static_cast<int>(scenePosition.x()), static_cast<int>(scenePosition.y()));
    QSignalSpy pressed(forceArmButton, SIGNAL(pressed()));
    QVERIFY(pressed.isValid());
    QSignalSpy holdReleased(forceArmButton, SIGNAL(released()));
    QSignalSpy holdCanceled(forceArmButton, SIGNAL(canceled()));
    QSignalSpy holdProgress(forceArmButton, SIGNAL(progressChanged()));
    QVERIFY(holdReleased.isValid());
    QVERIFY(holdCanceled.isValid());
    QVERIFY(holdProgress.isValid());
    trace(QStringLiteral("held press at (%1, %2)").arg(pressPosition.x()).arg(pressPosition.y()));
    {
        QTest::mouseMove(_window, pressPosition);
        QTest::mousePress(_window, Qt::LeftButton, Qt::NoModifier, pressPosition);
        const auto releaseMouse =
            qScopeGuard([&]() { QTest::mouseRelease(_window, Qt::LeftButton, Qt::NoModifier, pressPosition); });
        QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, 1000);
        QVERIFY2(activated.count() == 1 || (forceArmButton && forceArmButton->property("pressed").toBool()),
                 "Native mouse hold must actually press the force-arm button");
        trace(QStringLiteral("held press received by force-arm button"));
        // Qt's threaded scene graph needs a running event loop to drive this transition.
        // QSignalSpy waits for the real activation without altering the animation clock.
        const bool completed = !activated.isEmpty() || activated.wait(3000);
        const QString holdState = QStringLiteral(
                                      "Native hold: activated=%1 released=%2 canceled=%3 progressChanges=%4 pressed=%5 "
                                      "progress=%6 hovered=%7 windowActive=%8 exposed=%9 commands=%10 armed=%11 "
                                      "permission=%12 checked=%13")
                                      .arg(activated.count())
                                      .arg(holdReleased.count())
                                      .arg(holdCanceled.count())
                                      .arg(holdProgress.count())
                                      .arg(forceArmButton && forceArmButton->property("pressed").toBool())
                                      .arg(forceArmButton ? forceArmButton->property("progress").toDouble() : -1.0)
                                      .arg(forceArmButton && forceArmButton->property("hovered").toBool())
                                      .arg(_window->isActive())
                                      .arg(_window->isExposed())
                                      .arg(_link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM))
                                      .arg(vehicle->armed())
                                      .arg(status->property("_allowForceArm").toBool())
                                      .arg(forceArmButton && forceArmButton->property("checked").toBool());
        QVERIFY2(completed, qPrintable(holdState));
    }
    trace(QStringLiteral("held press activated once"));
    QTRY_COMPARE_WITH_TIMEOUT(_link->receivedMavCommandCount(MAV_CMD_COMPONENT_ARM_DISARM), 1, 3000);
    QVERIFY(!status->property("_allowForceArm").toBool());
    mavlink_message_t message{};
    QVERIFY(_link->lastReceivedMavlinkMessage(MAVLINK_MSG_ID_COMMAND_LONG, message));
    mavlink_command_long_t armCommand{};
    mavlink_msg_command_long_decode(&message, &armCommand);
    QCOMPARE(armCommand.command, static_cast<uint16_t>(MAV_CMD_COMPONENT_ARM_DISARM));
    QCOMPARE(armCommand.param1, 1.0f);
    QCOMPARE(armCommand.param2, 2989.0f);
    QCOMPARE(activated.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(vehicle->armed(), 3000);
    vehicle->setArmed(false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->armed() && !vehicle->armingStateUnconfirmed(), 3000);
    trace(QStringLiteral("command verified and Mock disarmed"));

    // Keep the native single-vehicle hold separate from the target-switch scenario.
    // Prepare the second vehicle with the drawer closed and retain the real ACK budget.
    QTRY_VERIFY_WITH_TIMEOUT(!vehicle->isMavCommandPending(vehicle->defaultComponentId(), MAV_CMD_SET_MESSAGE_INTERVAL),
                             5000);
    trace(QStringLiteral("preparing second Mock before target-switch scenario"));
    QPointer<MockLink> second;
    QPointer<Vehicle> other;
    const auto disconnectSecond = qScopeGuard([&]() {
        if (second)
            second->disconnect();
    });
    {
        // The unrelated connection announcement compiles a synchronous QML popup while
        // constructor-issued stream commands await ACK. Preserve its required log instead.
        auto* messagingEngine = qgcApp()->qmlAppEngine();
        QCOMPARE(messagingEngine, _engine);
        qgcApp()->setQmlAppEngine(nullptr);
        const auto restoreMessagingEngine = qScopeGuard([&]() { qgcApp()->setQmlAppEngine(messagingEngine); });
        second = secondIsSub ? MockLink::startAPMArduSubMockLink() : MockLink::startAPMArduCopterMockLink();
        QVERIFY(second);
        expectAppMessage(QRegularExpression(QStringLiteral("^showAppMessage:.* %1\"$").arg(second->vehicleId())));
        QTRY_VERIFY_WITH_TIMEOUT(MultiVehicleManager::instance()->getVehicleById(second->vehicleId()), 10000);
        verifyExpectedLogMessage();
        other = MultiVehicleManager::instance()->getVehicleById(second->vehicleId());
        QTRY_VERIFY_WITH_TIMEOUT(other->isInitialConnectComplete(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(other->parameterManager()->parametersReady(), 10000);
        QVERIFY(boundMockTimeoutMetadata(other));
        QTRY_VERIFY_WITH_TIMEOUT(
            !vehicle->isMavCommandPending(vehicle->defaultComponentId(), MAV_CMD_SET_MESSAGE_INTERVAL) &&
                !other->isMavCommandPending(other->defaultComponentId(), MAV_CMD_SET_MESSAGE_INTERVAL),
            5000);
    }
    QCOMPARE(qgcApp()->qmlAppEngine(), _engine);
    QCOMPARE(MultiVehicleManager::instance()->activeVehicle(), vehicle);
    trace(QStringLiteral("both Mocks ready and message interval queues drained"));

    QVERIFY(QMetaObject::invokeMethod(status, "dropMainStatusIndicator"));
    QVERIFY(clickButton(QStringLiteral("indicatorDrawerExpandButton")));
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(status->property("_allowForceArm").toBool());
    QVERIFY(QMetaObject::invokeMethod(_window, "closeIndicatorDrawer"));
    QTRY_VERIFY_WITH_TIMEOUT(!status->property("_allowForceArm").toBool(), 2000);
    QVERIFY(QMetaObject::invokeMethod(status, "dropMainStatusIndicator"));
    QVERIFY(clickButton(QStringLiteral("indicatorDrawerExpandButton")));
    QVERIFY(verifyChecked(QStringLiteral("mainStatus_allowForceArm"), false, QStringLiteral("reopened")));
    QVERIFY(clickButton(QStringLiteral("mainStatus_allowForceArm")));
    QVERIFY(status->property("_allowForceArm").toBool());
    trace(QStringLiteral("switching active Mock"));
    MultiVehicleManager::instance()->setActiveVehicle(other);
    QTRY_COMPARE_WITH_TIMEOUT(MultiVehicleManager::instance()->activeVehicle(), other.data(), 2000);
    QVERIFY(!status->property("_allowForceArm").toBool());
    QVERIFY(QMetaObject::invokeMethod(_window, "closeIndicatorDrawer"));
    QTRY_VERIFY_WITH_TIMEOUT(!findVisibleItem(_rootItem, QStringLiteral("mainStatus_allowForceArm"), 0), 2000);
}

void SetupAvailabilityTest::_summaryNavigation()
{
    _startConnectedUi();
    if (QTest::currentTestFailed())
        return;
    QVERIFY(QMetaObject::invokeMethod(_window, "showVehicleConfig"));
    auto* config = findVisibleItem(_rootItem, QStringLiteral("vehicleConfig_root"), 5000);
    QVERIFY(config);
    QVERIFY(config->property("_fullParameterVehicleAvailable").toBool());
    QCOMPARE(config->property("_selectedComponentIndex").toInt(), -1);
    const auto components = MultiVehicleManager::instance()->activeVehicle()->autopilotPlugin()->vehicleComponents();
    int summaryIndex = -1;
    for (int index = 0; index < components.size(); ++index) {
        auto* component = components[index].value<VehicleComponent*>();
        if (component && !component->summaryQmlSource().isEmpty() && !component->setupSource().isEmpty()) {
            summaryIndex = index;
            break;
        }
    }
    QVERIFY2(summaryIndex >= 0, "Mock vehicle must have a visible summary component with a setup page");
    const QString summaryButtonName = QStringLiteral("vehicleSummary_component_%1").arg(summaryIndex);
    auto* summaryButton = findVisibleItem(_rootItem, summaryButtonName, 5000);
    QVERIFY(summaryButton);
    QVERIFY(clickButton(summaryButtonName));
    QTRY_COMPARE_WITH_TIMEOUT(config->property("_selectedComponentIndex").toInt(), summaryIndex, 2000);
}

void SetupAvailabilityTest::_replayCloseKeepsOrdinaryVehicle()
{
    _startConnectedUi();
    if (QTest::currentTestFailed())
        return;
    auto* setting = SettingsManager::instance()->flyViewSettings()->showLogReplayStatusBar();
    const QVariant previous = setting->rawValue();
    const auto restore = qScopeGuard([&]() { setting->setRawValue(previous); });
    setting->setRawValue(true);
    auto* vehicle = MultiVehicleManager::instance()->activeVehicle();
    QVERIFY(vehicle);
    QVERIFY(clickButton(QStringLiteral("logReplay_closeButton")));
    QVERIFY(!setting->rawValue().toBool());
    QCOMPARE(MultiVehicleManager::instance()->activeVehicle(), vehicle);
    QVERIFY(_link && _link->isConnected());
}

UT_REGISTER_TEST(SetupAvailabilityTest, TestLabel::Integration)

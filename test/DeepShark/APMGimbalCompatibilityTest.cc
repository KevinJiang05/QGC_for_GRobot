#include "APMGimbalCompatibilityTest.h"

#include <QtQml/QQmlComponent>
#include <QtQml/QQmlEngine>
#include <QtQuick/QQuickItem>
#include <memory>

#include "ColoredSvgImageProvider.h"
#include "Fact.h"
#include "QGCCorePlugin.h"

void APMGimbalCompatibilityTest::_parameterFamilies_data()
{
    QTest::addColumn<QStringList>("parameterNames");
    QTest::addColumn<int>("instance");
    QTest::addColumn<int>("instanceCount");
    QTest::addColumn<bool>("legacy");
    QTest::addColumn<bool>("available");
    QTest::addColumn<QString>("pitchMinName");
    QTest::newRow("legacy") << QStringList{"MNT_TYPE",       "MNT_DEFLT_MODE", "MNT_ANGMIN_TIL",
                                           "MNT_RC_IN_TILT", "MNT_STAB_TILT",  "MNT_JSTICK_SPD"}
                            << 1 << 1 << true << true << QStringLiteral("MNT_ANGMIN_TIL");
    QTest::newRow("legacy-optional-missing") << QStringList{"MNT_RC_IN_TILT"} << 1 << 1 << true << true << QString();
    QTest::newRow("modern-two-instances") << QStringList{"MNT1_TYPE", "MNT1_DEFLT_MODE", "MNT1_PITCH_MIN",
                                                         "MNT2_TYPE", "MNT2_DEFLT_MODE", "MNT2_PITCH_MIN"}
                                          << 2 << 2 << false << true << QStringLiteral("MNT2_PITCH_MIN");
    QTest::newRow("modern-preferred") << QStringList{"MNT1_TYPE", "MNT1_DEFLT_MODE", "MNT1_PITCH_MIN",
                                                     "MNT_TYPE",  "MNT_DEFLT_MODE",  "MNT_ANGMIN_TIL"}
                                      << 1 << 1 << false << true << QStringLiteral("MNT1_PITCH_MIN");
    QTest::newRow("modern-needs-reboot") << QStringList{"MNT1_TYPE"} << 1 << 1 << false << false << QString();
    QTest::newRow("unsupported") << QStringList{} << 1 << 0 << false << false << QString();
}

void APMGimbalCompatibilityTest::_parameterFamilies()
{
    QFETCH(QStringList, parameterNames);
    QFETCH(int, instance);
    QFETCH(int, instanceCount);
    QFETCH(bool, legacy);
    QFETCH(bool, available);
    QFETCH(QString, pitchMinName);
    QObject factOwner;
    QVariantMap facts;
    for (const auto& name : parameterNames) {
        auto* const fact = new Fact(1, name, FactMetaData::valueTypeDouble, &factOwner);
        fact->setRawValue(name.contains(QStringLiteral("ANGMIN")) ? -4500.0 : 1.0);
        QQmlEngine::setObjectOwnership(fact, QQmlEngine::CppOwnership);
        facts.insert(name, QVariant::fromValue(fact));
    }
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.AutoPilotPlugins.APM
Item {
    property var fixtureFacts: ({})
    property int selectedInstance: 1
    property int missingReports: 0
    QtObject {
        id: fixtureController
        function parameterExists(componentId, name) { return fixtureFacts[name] !== undefined }
        function getParameterFact(componentId, name, reportMissing) {
            if (parameterExists(componentId, name)) return fixtureFacts[name]
            if (reportMissing !== false) missingReports++
            return null
        }
    }
    APMGimbalParams {
        objectName: "gimbalParams"
        controller: fixtureController
        instance: selectedInstance
    }
    APMGimbalInstance {
        objectName: "gimbalInstance"
        parameterController: fixtureController
        instance: selectedInstance
        width: 1200
    }
})",
                      QUrl(QStringLiteral("qrc:/gimbal-compatibility-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantMap initialProperties{{QStringLiteral("fixtureFacts"), facts},
                                        {QStringLiteral("selectedInstance"), instance}};
    std::unique_ptr<QObject> root(component.createWithInitialProperties(initialProperties));
    QVERIFY2(root, qPrintable(component.errorString()));
    auto* const params = root->findChild<QObject*>(QStringLiteral("gimbalParams"));
    QVERIFY(params);
    QVERIFY(root->findChild<QObject*>(QStringLiteral("gimbalInstance")));
    QCOMPARE(params->property("instanceCount").toInt(), instanceCount);
    QCOMPARE(params->property("legacyParameters").toBool(), legacy);
    QCOMPARE(params->property("paramsAvailable").toBool(), available);
    QCOMPARE(root->property("missingReports").toInt(), 0);
    auto* const pitchMin = qvariant_cast<Fact*>(params->property("pitchMinFact"));
    if (pitchMinName.isEmpty()) {
        QVERIFY(!pitchMin);
    } else {
        QCOMPARE(pitchMin, qvariant_cast<Fact*>(facts.value(pitchMinName)));
        // Selecting a compatible Fact must preserve its original raw unit and must not write it.
        QCOMPARE(pitchMin->rawValue().toDouble(), legacy ? -4500.0 : 1.0);
    }
    if (legacy) {
        QCOMPARE(qvariant_cast<Fact*>(params->property("pitchInputFact")),
                 qvariant_cast<Fact*>(facts.value("MNT_RC_IN_TILT")));
        QVERIFY(!qvariant_cast<Fact*>(params->property("rcRateFact")));
    }
    for (const auto& name : parameterNames) {
        QCOMPARE(qvariant_cast<Fact*>(facts.value(name))->rawValue().toDouble(),
                 name.contains(QStringLiteral("ANGMIN")) ? -4500.0 : 1.0);
    }
}

UT_REGISTER_TEST(APMGimbalCompatibilityTest, TestLabel::Unit)

void APMGimbalCompatibilityTest::_outputAssignments_data()
{
    QTest::addColumn<bool>("legacyMount");
    QTest::addColumn<QString>("outputPrefix");
    QTest::addColumn<int>("functionValue");
    for (const int functionValue : {7, 6, 8}) {
        const QByteArray suffix = QByteArray::number(functionValue);
        QTest::newRow(("legacy-RC-" + suffix).constData()) << true << QStringLiteral("RC") << functionValue;
        QTest::newRow(("legacy-SERVO-" + suffix).constData()) << true << QStringLiteral("SERVO") << functionValue;
        QTest::newRow(("modern-SERVO-" + suffix).constData()) << false << QStringLiteral("SERVO") << functionValue;
    }
}

void APMGimbalCompatibilityTest::_outputAssignments()
{
    QFETCH(bool, legacyMount);
    QFETCH(QString, outputPrefix);
    QFETCH(int, functionValue);
    QObject factOwner;
    QVariantMap facts;
    const auto addFact = [&](const QString& name, int value) {
        auto* const fact = new Fact(1, name, FactMetaData::valueTypeInt32, &factOwner);
        fact->setRawValue(value);
        QQmlEngine::setObjectOwnership(fact, QQmlEngine::CppOwnership);
        facts.insert(name, QVariant::fromValue(fact));
        return fact;
    };
    const QString mountPrefix = legacyMount ? QStringLiteral("MNT_") : QStringLiteral("MNT1_");
    addFact(mountPrefix + "TYPE", 1);
    addFact(mountPrefix + "DEFLT_MODE", 0);
    QList<Fact*> outputs;
    for (int channel = 1; channel <= 8; ++channel) {
        outputs.append(addFact(outputPrefix + QString::number(channel) + "_FUNCTION",
                               channel == 5 ? functionValue : (channel == 4 ? 25 : 0)));
    }
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.AutoPilotPlugins.APM
Item {
    property var fixtureFacts: ({})
    property int missingReports: 0
    QtObject {
        id: fixtureController
        function parameterExists(componentId, name) { return fixtureFacts[name] !== undefined }
        function getParameterFact(componentId, name, reportMissing) {
            if (parameterExists(componentId, name)) return fixtureFacts[name]
            if (reportMissing !== false) missingReports++
            return null
        }
    }
    APMGimbalInstance {
        parameterController: fixtureController
        width: 1200
    }
})",
                      QUrl(QStringLiteral("qrc:/gimbal-output-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    const QVariantMap initialProperties{{QStringLiteral("fixtureFacts"), facts}};
    const auto createPage = [&]() {
        return std::unique_ptr<QObject>(component.createWithInitialProperties(initialProperties));
    };
    auto page = createPage();
    QVERIFY2(page, qPrintable(component.errorString()));
    const QString comboName = QStringLiteral("gimbalOutputChannel_%1").arg(functionValue);
    const auto findCombo = [&](auto&& self, QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == comboName) {
            return item;
        }
        for (auto* child : item->childItems()) {
            if (auto* match = self(self, child)) {
                return match;
            }
        }
        return nullptr;
    };
    auto* combo = findCombo(findCombo, qobject_cast<QQuickItem*>(page.get()));
    QVERIFY(combo);
    QCOMPARE(combo->property("currentIndex").toInt(), 5);

    // Activate the real combo handler, then inspect the same Facts after recreating the page.
    QVERIFY(combo->setProperty("currentIndex", 6));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 6)));
    QCOMPARE(outputs[4]->rawValue().toInt(), 0);
    QCOMPARE(outputs[5]->rawValue().toInt(), functionValue);
    QCOMPARE(outputs[3]->rawValue().toInt(), 25);
    page.reset();
    page = createPage();
    QVERIFY2(page, qPrintable(component.errorString()));
    combo = findCombo(findCombo, qobject_cast<QQuickItem*>(page.get()));
    QVERIFY(combo);
    QCOMPARE(combo->property("currentIndex").toInt(), 6);

    // Also clean up duplicate assignments left behind by the earlier implementation.
    outputs[4]->setRawValue(functionValue);
    QVERIFY(combo->setProperty("currentIndex", 0));
    QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, 0)));
    for (int channel = 1; channel <= outputs.size(); ++channel) {
        QCOMPARE(outputs[channel - 1]->rawValue().toInt(), channel == 4 ? 25 : 0);
    }
    page.reset();
    page = createPage();
    QVERIFY2(page, qPrintable(component.errorString()));
    combo = findCombo(findCombo, qobject_cast<QQuickItem*>(page.get()));
    QVERIFY(combo);
    QCOMPARE(combo->property("currentIndex").toInt(), 0);
    QCOMPARE(page->property("missingReports").toInt(), 0);
}

void APMGimbalCompatibilityTest::_legacyInputSideEffectNeedsUserSelection()
{
    QObject factOwner;
    QVariantMap facts;
    const auto addFact = [&](const QString& name, int value) {
        auto* const fact = new Fact(1, name, FactMetaData::valueTypeInt32, &factOwner);
        fact->setRawValue(value);
        QQmlEngine::setObjectOwnership(fact, QQmlEngine::CppOwnership);
        facts.insert(name, QVariant::fromValue(fact));
        return fact;
    };
    addFact(QStringLiteral("MNT_TYPE"), 1);
    Fact* const defaultMode = addFact(QStringLiteral("MNT_DEFLT_MODE"), 0);
    Fact* const tiltInput = addFact(QStringLiteral("MNT_RC_IN_TILT"), 0);
    tiltInput->setEnumInfo({QStringLiteral("Disabled"), QStringLiteral("RC6"), QStringLiteral("RC8")}, {0, 6, 8});
    QGCCorePlugin::instance()->init();
    QQmlEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qml"));
    engine.addImageProvider(QLatin1String(ColoredSvgImageProvider::ProviderId), new ColoredSvgImageProvider());
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import QGroundControl
import QGroundControl.AutoPilotPlugins.APM
Item {
    property var fixtureFacts: ({})
    QtObject {
        id: fixtureController
        function parameterExists(componentId, name) { return fixtureFacts[name] !== undefined }
        function getParameterFact(componentId, name, reportMissing) {
            return parameterExists(componentId, name) ? fixtureFacts[name] : null
        }
    }
    APMGimbalInstance {
        parameterController: fixtureController
        width: 1200
    }
})",
                      QUrl(QStringLiteral("qrc:/gimbal-legacy-input-fixture.qml")));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    std::unique_ptr<QObject> page(component.createWithInitialProperties({{QStringLiteral("fixtureFacts"), facts}}));
    QVERIFY2(page, qPrintable(component.errorString()));

    // A value arriving from the vehicle or from the parameter editor is not a selection on this page.
    tiltInput->setRawValue(6);
    QCoreApplication::processEvents();
    QCOMPARE(defaultMode->rawValue().toInt(), 0);

    const auto findInput = [](auto&& self, QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == QStringLiteral("gimbalLegacyInput_0")) {
            return item;
        }
        for (auto* child : item->childItems()) {
            if (auto* match = self(self, child)) {
                return match;
            }
        }
        return nullptr;
    };
    auto* const input = findInput(findInput, qobject_cast<QQuickItem*>(page.get()));
    QVERIFY(input);
    // Activating the inner combo box is what a selection does: it writes the input and reaches the page handler.
    auto* const comboBox = qvariant_cast<QObject*>(input->property("comboBox"));
    QVERIFY(comboBox);
    QVERIFY(QMetaObject::invokeMethod(comboBox, "activated", Q_ARG(int, 2)));
    QCOMPARE(tiltInput->rawValue().toInt(), 8);
    QCOMPARE(defaultMode->rawValue().toInt(), 3);
}

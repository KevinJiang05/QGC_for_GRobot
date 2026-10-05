#include "DeepSharkAuvControllerTest.h"

#include "DeepSharkAuvController.h"

#include <QtCore/QFile>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTemporaryDir>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

namespace {
QJsonObject planWithItems(const QJsonArray &items)
{
    return {
        {QStringLiteral("fileType"), QStringLiteral("Plan")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("mission"), QJsonObject{
             {QStringLiteral("version"), 2},
             {QStringLiteral("items"), items},
         }},
    };
}

QString writeJsonFile(QTemporaryDir &directory, const QString &name, const QJsonObject &json)
{
    const QString path = directory.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return {};
    }
    if (file.write(QJsonDocument(json).toJson(QJsonDocument::Compact)) < 0) {
        return {};
    }
    file.close();
    return path;
}

QJsonObject coordinateItem(double latitude, double longitude, double altitude, int command = 16)
{
    return {
        {QStringLiteral("type"), QStringLiteral("SimpleItem")},
        {QStringLiteral("command"), command},
        {QStringLiteral("coordinate"), QJsonArray{latitude, longitude, altitude}},
    };
}

QJsonObject paramsItem(double latitude, double longitude, double altitude, int command = 16)
{
    return {
        {QStringLiteral("type"), QStringLiteral("SimpleItem")},
        {QStringLiteral("command"), command},
        {QStringLiteral("params"), QJsonArray{0.0, 0.0, 0.0, QJsonValue(QJsonValue::Null), latitude, longitude, altitude}},
    };
}

QJsonObject nonPositionalItem(int command)
{
    const QJsonValue nullValue(QJsonValue::Null);
    return {
        {QStringLiteral("type"), QStringLiteral("SimpleItem")},
        {QStringLiteral("command"), command},
        {QStringLiteral("params"), QJsonArray{0.0, 0.0, 0.0, nullValue, nullValue, nullValue, nullValue}},
    };
}
}

void DeepSharkAuvControllerTest::_importsSimpleWaypointFormats()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeJsonFile(directory,
                                       QStringLiteral("mixed-simple.plan"),
                                       planWithItems({coordinateItem(40.3068, 116.6116, -5.0),
                                                      paramsItem(40.3072, 116.6122, -7.0)}));
    QVERIFY(!path.isEmpty());

    DeepSharkAuvController controller;
    QSignalSpy missionChanged(&controller, &DeepSharkAuvController::missionChanged);
    QVERIFY(controller.importPlan(path));

    QCOMPARE(missionChanged.count(), 1);
    QCOMPARE(controller.missionName(), QStringLiteral("mixed-simple"));
    QCOMPARE(controller.waypointCount(), 2);
    QVERIFY(controller.missionError().isEmpty());
    QVERIFY(controller.totalDistance() > 0.0);

    const QVariantMap first = controller.waypoints().at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("sequence")).toInt(), 1);
    QCOMPARE(first.value(QStringLiteral("latitude")).toDouble(), 40.3068);
    QCOMPARE(first.value(QStringLiteral("longitude")).toDouble(), 116.6116);
    QCOMPARE(first.value(QStringLiteral("depth")).toDouble(), 5.0);
    QCOMPARE(first.value(QStringLiteral("command")).toInt(), 16);

    const QVariantMap second = controller.waypoints().at(1).toMap();
    QCOMPARE(second.value(QStringLiteral("sequence")).toInt(), 2);
    QCOMPARE(second.value(QStringLiteral("depth")).toDouble(), 7.0);
}

void DeepSharkAuvControllerTest::_rejectsComplexItemsWithoutReplacingMission()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QJsonObject complexItem{
        {QStringLiteral("type"), QStringLiteral("ComplexItem")},
        {QStringLiteral("complexItemType"), QStringLiteral("survey")},
        {QStringLiteral("coordinate"), QJsonArray{40.4, 116.7, -6.0}},
    };
    const QString path = writeJsonFile(directory,
                                       QStringLiteral("complex.plan"),
                                       planWithItems({coordinateItem(40.3, 116.6, -4.0), complexItem}));
    QVERIFY(!path.isEmpty());

    DeepSharkAuvController controller;
    const QString previousName = controller.missionName();
    const QVariantList previousWaypoints = controller.waypoints();

    QVERIFY(!controller.importPlan(path));
    QVERIFY(controller.missionError().contains(QStringLiteral("复杂任务项")));
    QCOMPARE(controller.missionName(), previousName);
    QCOMPARE(controller.waypoints(), previousWaypoints);
}

void DeepSharkAuvControllerTest::_ignoresNonPositionalItems()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = writeJsonFile(directory,
                                       QStringLiteral("non-positional.plan"),
                                       planWithItems({nonPositionalItem(205),
                                                      coordinateItem(40.3068, 116.6116, -3.0)}));
    QVERIFY(!path.isEmpty());

    DeepSharkAuvController controller;
    QVERIFY(controller.importPlan(path));
    QCOMPARE(controller.waypointCount(), 1);
    QCOMPARE(controller.waypoints().constFirst().toMap().value(QStringLiteral("depth")).toDouble(), 3.0);
}

void DeepSharkAuvControllerTest::_rejectsInvalidSchema()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());

    DeepSharkAuvController controller;
    const QString missingMissionPath = writeJsonFile(directory,
                                                     QStringLiteral("missing-mission.plan"),
                                                     QJsonObject{{QStringLiteral("fileType"), QStringLiteral("Plan")}});
    QVERIFY(!missingMissionPath.isEmpty());
    QVERIFY(!controller.importPlan(missingMissionPath));
    QVERIFY(controller.missionError().contains(QStringLiteral("mission 必须是对象")));

    const QString badItemsPath = writeJsonFile(directory,
                                               QStringLiteral("bad-items.plan"),
                                               QJsonObject{
                                                   {QStringLiteral("fileType"), QStringLiteral("Plan")},
                                                   {QStringLiteral("mission"), QJsonObject{{QStringLiteral("items"), QStringLiteral("invalid")}}},
                                               });
    QVERIFY(!badItemsPath.isEmpty());
    QVERIFY(!controller.importPlan(badItemsPath));
    QVERIFY(controller.missionError().contains(QStringLiteral("mission.items 必须是数组")));

    QJsonObject badCoordinate = coordinateItem(40.0, 116.0, -3.0);
    badCoordinate.insert(QStringLiteral("coordinate"), QStringLiteral("invalid"));
    const QString badCoordinatePath = writeJsonFile(directory,
                                                    QStringLiteral("bad-coordinate.plan"),
                                                    planWithItems({badCoordinate}));
    QVERIFY(!badCoordinatePath.isEmpty());
    QVERIFY(!controller.importPlan(badCoordinatePath));
    QVERIFY(controller.missionError().contains(QStringLiteral("coordinate 必须是数组")));
}

UT_REGISTER_TEST(DeepSharkAuvControllerTest, TestLabel::Unit)

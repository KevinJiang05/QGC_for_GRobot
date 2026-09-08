#pragma once

#include "UnitTest.h"

class DeepSharkAuvControllerTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _importsSimpleWaypointFormats();
    void _rejectsComplexItemsWithoutReplacingMission();
    void _ignoresNonPositionalItems();
    void _rejectsInvalidSchema();
};

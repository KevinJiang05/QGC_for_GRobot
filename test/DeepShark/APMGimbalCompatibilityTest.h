#pragma once

#include "UnitTest.h"

class APMGimbalCompatibilityTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _parameterFamilies_data();
    void _parameterFamilies();
    void _outputAssignments_data();
    void _outputAssignments();
};

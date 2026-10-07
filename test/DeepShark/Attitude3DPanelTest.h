#pragma once

#include "QmlUITestBase.h"

class Attitude3DPanelTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _poseAndLevelCamera_data();
    void _poseAndLevelCamera();
    void _headingWrapUsesShortPath();
    void _viewStateSurvivesPanelRecreation();
    void _missingAndLostTelemetry();
    void _inertiaExposesTheSideAndSettles();
    void _springIsFrameRateIndependent();
    void _depthUsesSurfaceZero();
};

class Attitude3DUITest : public QmlUITestBase
{
    Q_OBJECT

private slots:
    void _previewWorkspaceRoundTrip();
};

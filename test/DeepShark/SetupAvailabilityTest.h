#pragma once

#include "QmlUITestBase.h"

class SetupAvailabilityTest : public QmlUITestBase
{
    Q_OBJECT

protected slots:
    void cleanup() override;

private slots:
    void _forceArmPermission_data();
    void _forceArmPermission();
    void _summaryNavigation();
    void _replayCloseKeepsOrdinaryVehicle();
    // The full UI must select its Controls style before this slot creates a small QML engine.
    void _validationOwnership();
    void _linkEditorWaitsForSettingsPage();
    void _linkEditorSavesExistingLink();

private:
    void _startConnectedUi();
    QPointer<MockLink> _link;
};

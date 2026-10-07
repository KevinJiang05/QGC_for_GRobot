#pragma once

#include "UnitTest.h"

class Fact;

class AppSettingsTest : public UnitTest
{
    Q_OBJECT

private slots:
    void _preferredFirmwareClassEnumFiltered();
    void _offlineEditingFirmwareClassEnumFiltered();
    void _chineseTranslations_data();
    void _chineseTranslations();
    void _setupSafetyOverride_data();
    void _setupSafetyOverride();

private:
    void _verifyFirmwareClassEnumFiltered(Fact *fact);
};

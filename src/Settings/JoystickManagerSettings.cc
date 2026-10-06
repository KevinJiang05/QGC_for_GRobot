/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "JoystickManagerSettings.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSettings>

DECLARE_SETTINGGROUP(JoystickManager, "JoystickManager")
{
    QSettings settings;
    const QString prefix = QString::fromLatin1(settingsGroup) + '/';
    if (!settings.contains(prefix + activeJoystickNameName) && settings.contains(prefix + "ActiveJoystick")) {
        settings.setValue(prefix + activeJoystickNameName, settings.value(prefix + "ActiveJoystick"));
    }
    if (!settings.contains(prefix + joystickEnabledVehiclesIdsName)) {
        QStringList enabledIds;
        bool hasLegacySettings = false;
        const QRegularExpression vehicleGroup(QStringLiteral("^Vehicle([1-9][0-9]{0,2})$"));
        for (const QString& group : settings.childGroups()) {
            const auto match = vehicleGroup.match(group);
            if (!match.hasMatch() || match.captured(1).toInt() > 255) {
                continue;
            }
            const QString key = group + "/JoystickEnabled";
            if (settings.contains(key)) {
                hasLegacySettings = true;
                if (settings.value(key).toBool()) {
                    enabledIds.append(match.captured(1));
                }
            }
        }
        if (hasLegacySettings) {
            settings.setValue(prefix + joystickEnabledVehiclesIdsName, enabledIds.join(','));
        }
    }
}

DECLARE_SETTINGSFACT(JoystickManagerSettings, activeJoystickName)
DECLARE_SETTINGSFACT(JoystickManagerSettings, joystickEnabledVehiclesIds)

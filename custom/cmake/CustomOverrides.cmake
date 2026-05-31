# DeepShark custom build overrides.
#
# Keep this file intentionally minimal. Do not disable ArduPilot/APM, MAVLink
# dialects, or firmware plugin factories here.

set(QGC_APP_NAME "QGC_KevinJiang" CACHE STRING "App Name" FORCE)
set(QGC_APP_COPYRIGHT "Copyright (c) 2026 KevinJiang. Based on QGroundControl." CACHE STRING "Copyright" FORCE)
set(QGC_APP_DESCRIPTION "KevinJiang custom ground control app" CACHE STRING "Description" FORCE)
set(QGC_ORG_NAME "KevinJiang" CACHE STRING "Org Name" FORCE)
set(QGC_ORG_DOMAIN "kevinjiang.local" CACHE STRING "Domain" FORCE)
set(QGC_PACKAGE_NAME "com.kevinjiang.qgc" CACHE STRING "Package Name" FORCE)
set(QGC_ANDROID_PACKAGE_NAME "${QGC_PACKAGE_NAME}" CACHE STRING "Android Package Name" FORCE)
set(QGC_MACOS_BUNDLE_ID "${QGC_PACKAGE_NAME}" CACHE STRING "MacOS Bundle ID" FORCE)
set(QGC_APP_VERSION_OVERRIDE "1.0.0")
set(QGC_APP_VERSION_STR_OVERRIDE "1.0.0")

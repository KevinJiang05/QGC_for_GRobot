import QtQuick

QtObject {
    enum ViewMode { Follow, Fixed, Top, Left, Right, Front, Free, HardFollow }

    property int viewMode: Attitude3DViewState.Follow
    property real cameraPitch: -18
    property real cameraYaw: 0
    property real cameraDistance: 650
    readonly property bool followsHeading: viewMode !== Attitude3DViewState.Fixed
                                           && viewMode !== Attitude3DViewState.Free
    readonly property bool inertialFollow: viewMode === Attitude3DViewState.Follow
    property real followYaw: 0
    property real followYawVelocity: 0
    property real followHeight: 0
    property real followHeightVelocity: 0
    property real vehicleY: 0
    property bool hasDepth: false
    property bool followInitialized: false
    property var trackedVehicle: null
    property real lastFollowUpdate: 0

    function angleDifference(angle, reference) {
        return ((angle - reference + 180) % 360 + 360) % 360 - 180
    }

    function initializeFollow(yaw, height, vehicle) {
        if (!followInitialized || trackedVehicle !== vehicle || Date.now() - lastFollowUpdate > 1000) {
            followYaw = yaw
            followHeight = height
            followYawVelocity = 0
            followHeightVelocity = 0
            followInitialized = true
            trackedVehicle = vehicle
        }
        lastFollowUpdate = Date.now()
    }

    function advanceFollow(seconds, yaw, height, yawLimit, vehicle) {
        if (!isFinite(yaw) || !isFinite(height) || !isFinite(seconds) || seconds < 0) {
            return
        }
        initializeFollow(yaw, height, vehicle)
        if (seconds === 0) {
            return
        }

        // Exact critically damped spring update, with bounded yaw and height lag.
        const dt = Math.min(seconds, 0.25)
        const frequency = 6
        const decay = Math.exp(-frequency * dt)
        const yawError = angleDifference(followYaw, yaw)
        const yawTerm = followYawVelocity + frequency * yawError
        const nextYawError = (yawError + yawTerm * dt) * decay
        followYaw += nextYawError - yawError
        followYawVelocity = (followYawVelocity - frequency * yawTerm * dt) * decay
        const limitedYawError = Math.max(-yawLimit, Math.min(yawLimit, nextYawError))
        followYaw += limitedYawError - nextYawError

        const heightError = followHeight - height
        const heightTerm = followHeightVelocity + frequency * heightError
        const nextHeightError = (heightError + heightTerm * dt) * decay
        followHeight = height + Math.max(-30, Math.min(30, nextHeightError))
        followHeightVelocity = (followHeightVelocity - frequency * heightTerm * dt) * decay
    }

    function selectView(mode) {
        if (mode < Attitude3DViewState.Follow || mode > Attitude3DViewState.HardFollow) {
            return
        }
        viewMode = mode
        cameraPitch = mode === Attitude3DViewState.Top ? -82 : -18
        cameraYaw = mode === Attitude3DViewState.Fixed ? -28
                  : mode === Attitude3DViewState.Left ? -90
                  : mode === Attitude3DViewState.Right ? 90
                  : mode === Attitude3DViewState.Front ? 180 : 0
        followInitialized = false
    }

    function enterFreeView(worldYaw) {
        cameraYaw = worldYaw
        viewMode = Attitude3DViewState.Free
    }

    function resetView() {
        cameraDistance = 650
        selectView(Attitude3DViewState.Follow)
    }
}

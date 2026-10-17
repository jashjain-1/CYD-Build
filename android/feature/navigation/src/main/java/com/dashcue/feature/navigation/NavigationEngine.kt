package com.dashcue.feature.navigation

import android.annotation.SuppressLint
import android.content.Context
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Bundle
import android.os.Looper
import com.dashcue.core.common.DashCueLog
import com.dashcue.core.protocol.DashCueFraming
import com.dashcue.v1.*
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlin.math.*

class NavigationEngine(
    private val context: Context? = null,
    private val routingClient: OsrmRoutingClient = OsrmRoutingClient(),
    private val scope: CoroutineScope,
    private val locationManager: LocationManager? = context?.getSystemService(Context.LOCATION_SERVICE) as? LocationManager,
    var onSpeedUpdated: ((Int) -> Unit)? = null
) {
    companion object {
        private const val TAG = "NavigationEngine"
        const val REROUTE_THROTTLE_MS = 10_000L // 10s reroute throttle
        const val DRIVING_LOCK_SPEED_KMH_X10 = 80 // 8.0 km/h
        const val OFF_ROUTE_FIXES_REQUIRED = 3
        const val STEP_ARRIVAL_DISTANCE_M = 25.0
    }

    private val _navState = MutableStateFlow<NavState?>(null)
    val navState: StateFlow<NavState?> = _navState.asStateFlow()

    private val _routeOverview = MutableStateFlow<RouteOverview?>(null)
    val routeOverview: StateFlow<RouteOverview?> = _routeOverview.asStateFlow()

    private val _currentRoute = MutableStateFlow<NavigationRoute?>(null)
    val currentRoute: StateFlow<NavigationRoute?> = _currentRoute.asStateFlow()

    private val _drivingLockActive = MutableStateFlow(false)
    val drivingLockActive: StateFlow<Boolean> = _drivingLockActive.asStateFlow()

    private var activeDestination: GeoPoint? = null
    private var currentStepIndex = 0
    private var consecutiveOffRouteFixes = 0
    private var lastRerouteTimeMs = 0L
    private var isNavigating = false

    private val locationListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            onNewLocation(location)
        }
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}
        override fun onProviderEnabled(provider: String) {}
        override fun onProviderDisabled(provider: String) {}
    }

    /**
     * Starts navigation towards the destination from the specified origin.
     */
    fun startNavigation(origin: GeoPoint, destination: GeoPoint) {
        isNavigating = true
        activeDestination = destination
        currentStepIndex = 0
        consecutiveOffRouteFixes = 0

        _navState.value = NavState.newBuilder()
            .setStatus(NavStatus.NAV_CALCULATING)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()

        startLocationUpdates()

        scope.launch {
            requestRoute(origin, destination)
        }
    }

    /**
     * Stops navigation and clears active state.
     */
    fun stopNavigation() {
        isNavigating = false
        activeDestination = null
        stopLocationUpdates()
        _drivingLockActive.value = false
        _currentRoute.value = null
        _routeOverview.value = null
        _navState.value = NavState.newBuilder()
            .setStatus(NavStatus.NAV_NONE)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()
    }

    private suspend fun requestRoute(origin: GeoPoint, destination: GeoPoint) {
        val result = routingClient.calculateRoute(origin, destination)
        result.onSuccess { route ->
            _currentRoute.value = route
            _routeOverview.value = route.overview
            currentStepIndex = 0
            consecutiveOffRouteFixes = 0
            lastRerouteTimeMs = System.currentTimeMillis()

            DashCueLog.i(TAG, "Route loaded: ${route.steps.size} steps, ${route.totalDistanceM} m")
            updateNavStateFromRoute(route, origin, null)
        }.onFailure { error ->
            DashCueLog.e(TAG, "Routing failed", error)
            _navState.value = NavState.newBuilder()
                .setStatus(NavStatus.NAV_ERROR)
                .setPhoneTsMs(System.currentTimeMillis())
                .build()
        }
    }

    /**
     * Handles incoming Location updates.
     */
    fun onNewLocation(location: Location) {
        // 1. Driving Lock check (speed >= 8.0 km/h)
        val speedKmhX10 = if (location.hasSpeed()) {
            (location.speed * 3.6 * 10).roundToInt()
        } else {
            -1
        }
        _drivingLockActive.value = (speedKmhX10 >= DRIVING_LOCK_SPEED_KMH_X10)
        onSpeedUpdated?.invoke(speedKmhX10)

        if (!isNavigating) return

        val route = _currentRoute.value
        val dest = activeDestination
        if (route == null || dest == null) return

        val userPoint = GeoPoint(location.latitude, location.longitude)
        val accuracy = if (location.hasAccuracy()) location.accuracy.toDouble() else 10.0

        // 2. Off-Route Detection: distance to polyline > max(30m, 2*accuracy)
        val (closestDistM, segmentIdx, offsetM) = computeDistanceToRoute(userPoint, route.points)
        val offRouteThreshold = max(30.0, 2.0 * accuracy)

        if (closestDistM > offRouteThreshold) {
            consecutiveOffRouteFixes++
            DashCueLog.w(TAG, "Off-route fix $consecutiveOffRouteFixes (dist=$closestDistM m, threshold=$offRouteThreshold m)")

            if (consecutiveOffRouteFixes >= OFF_ROUTE_FIXES_REQUIRED) {
                _navState.value = _navState.value?.toBuilder()
                    ?.setStatus(NavStatus.NAV_OFF_ROUTE)
                    ?.build()

                val now = System.currentTimeMillis()
                if (now - lastRerouteTimeMs >= REROUTE_THROTTLE_MS) {
                    lastRerouteTimeMs = now
                    DashCueLog.i(TAG, "Triggering automatic reroute...")
                    _navState.value = _navState.value?.toBuilder()
                        ?.setStatus(NavStatus.NAV_REROUTING)
                        ?.build()
                    scope.launch {
                        requestRoute(userPoint, dest)
                    }
                }
                return
            }
        } else {
            consecutiveOffRouteFixes = 0
        }

        // 3. Step Progression & Arrival
        advanceStepProgression(userPoint, route)

        // 4. Emit updated NavState
        buildAndEmitNavState(userPoint, location, route, segmentIdx, offsetM)
    }

    private fun advanceStepProgression(userPoint: GeoPoint, route: NavigationRoute) {
        val steps = route.steps
        if (steps.isEmpty()) return

        val currentStep = steps.getOrNull(currentStepIndex) ?: return
        val distToStep = distanceMeters(userPoint, currentStep.location)

        // Advance step if within threshold
        if (distToStep < STEP_ARRIVAL_DISTANCE_M) {
            if (currentStepIndex < steps.size - 1) {
                currentStepIndex++
                DashCueLog.i(TAG, "Advanced to step $currentStepIndex: ${steps[currentStepIndex].street}")
            } else {
                // Destination reached
                _navState.value = _navState.value?.toBuilder()
                    ?.setStatus(NavStatus.NAV_ARRIVED)
                    ?.setDistToNextM(0)
                    ?.setRemainingM(0)
                    ?.setRemainingS(0)
                    ?.build()
            }
        }
    }

    private fun buildAndEmitNavState(
        userPoint: GeoPoint,
        location: Location,
        route: NavigationRoute,
        segmentIdx: Int,
        offsetM: Double
    ) {
        val steps = route.steps
        val currentStep = steps.getOrNull(currentStepIndex)
        val followingStep = steps.getOrNull(currentStepIndex + 1)

        val distToNext = currentStep?.let { distanceMeters(userPoint, it.location).roundToInt() } ?: 0
        val distToFollowing = followingStep?.let { distanceMeters(userPoint, it.location).roundToInt() } ?: 0

        // Remaining distance calculation along route
        var remainingM = distToNext
        for (i in (currentStepIndex + 1) until steps.size) {
            remainingM += steps[i].distanceM.roundToInt()
        }

        val speed = if (location.hasSpeed() && location.speed > 1.0f) location.speed.toDouble() else 11.1 // ~40 km/h default
        val remainingS = (remainingM / speed).roundToInt()

        val speedKmhX10 = if (location.hasSpeed()) (location.speed * 3.6 * 10).roundToInt() else -1

        val state = NavState.newBuilder()
            .setRouteId(route.routeId.toInt())
            .setStatus(NavStatus.NAV_ACTIVE)
            .setNext(currentStep?.maneuver ?: Maneuver.MV_NONE)
            .setDistToNextM(distToNext)
            .setStreet(currentStep?.street ?: "")
            .setRoundaboutExit(currentStep?.roundaboutExit ?: 0)
            .setFollowing(followingStep?.maneuver ?: Maneuver.MV_NONE)
            .setDistToFollowingM(distToFollowing)
            .setRemainingM(remainingM)
            .setRemainingS(remainingS)
            .setRouteSegment(segmentIdx)
            .setSegmentOffsetM(offsetM.roundToInt())
            .setHeadingDeg(location.bearing)
            .setSpeedKmhX10(speedKmhX10)
            .setGpsAccuracyM(location.accuracy)
            .setHasLiveTraffic(false)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()

        _navState.value = state
    }

    private fun updateNavStateFromRoute(route: NavigationRoute, origin: GeoPoint, location: Location?) {
        val steps = route.steps
        val currentStep = steps.getOrNull(0)
        val followingStep = steps.getOrNull(1)

        val state = NavState.newBuilder()
            .setRouteId(route.routeId.toInt())
            .setStatus(NavStatus.NAV_ACTIVE)
            .setNext(currentStep?.maneuver ?: Maneuver.MV_NONE)
            .setDistToNextM(currentStep?.distanceM?.roundToInt() ?: 0)
            .setStreet(currentStep?.street ?: "")
            .setRoundaboutExit(currentStep?.roundaboutExit ?: 0)
            .setFollowing(followingStep?.maneuver ?: Maneuver.MV_NONE)
            .setDistToFollowingM(followingStep?.distanceM?.roundToInt() ?: 0)
            .setRemainingM(route.totalDistanceM.roundToInt())
            .setRemainingS(route.totalDurationS.roundToInt())
            .setRouteSegment(0)
            .setSegmentOffsetM(0)
            .setHeadingDeg(location?.bearing ?: 0f)
            .setSpeedKmhX10(location?.let { if (it.hasSpeed()) (it.speed * 3.6 * 10).roundToInt() else -1 } ?: -1)
            .setGpsAccuracyM(location?.accuracy ?: 5f)
            .setHasLiveTraffic(false)
            .setPhoneTsMs(System.currentTimeMillis())
            .build()

        _navState.value = state
    }

    /**
     * Dispatches an incoming NavCmd from the HUD.
     */
    fun handleNavCommand(cmd: NavCmd): CmdResult {
        return when (cmd.action) {
            NavCommand.NC_END_NAVIGATION -> {
                DashCueLog.i(TAG, "HUD requested end navigation (commandId=${cmd.commandId})")
                stopNavigation()
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_CONFIRMED)
                    .setObservedAtMs(System.currentTimeMillis())
                    .build()
            }
            NavCommand.NC_REROUTE -> {
                DashCueLog.i(TAG, "HUD requested reroute (commandId=${cmd.commandId})")
                val dest = activeDestination
                val current = _navState.value
                if (dest != null) {
                    lastRerouteTimeMs = System.currentTimeMillis()
                    // If we have a location, trigger route
                    _navState.value = current?.toBuilder()
                        ?.setStatus(NavStatus.NAV_REROUTING)
                        ?.build()
                }
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_CONFIRMED)
                    .setObservedAtMs(System.currentTimeMillis())
                    .build()
            }
            else -> {
                CmdResult.newBuilder()
                    .setCommandId(cmd.commandId)
                    .setStatus(CmdResult.Status.ST_FAILED)
                    .setReason(ErrorCode.EC_UNSUPPORTED_VALUE)
                    .build()
            }
        }
    }

    @SuppressLint("MissingPermission")
    private fun startLocationUpdates() {
        val lm = locationManager ?: return
        try {
            if (lm.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.GPS_PROVIDER, 1000L, 0f, locationListener, Looper.getMainLooper())
            } else if (lm.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
                lm.requestLocationUpdates(LocationManager.NETWORK_PROVIDER, 1000L, 0f, locationListener, Looper.getMainLooper())
            }
        } catch (e: SecurityException) {
            DashCueLog.w(TAG, "Missing location permissions", e)
        }
    }

    private fun stopLocationUpdates() {
        val lm = locationManager ?: return
        try {
            lm.removeUpdates(locationListener)
        } catch (e: Exception) {
            DashCueLog.w(TAG, "Error removing location updates", e)
        }
    }

    /**
     * Computes the minimum distance from point to the polyline and along-route segment indices.
     */
    fun computeDistanceToRoute(userPoint: GeoPoint, points: List<GeoPoint>): Triple<Double, Int, Double> {
        if (points.size < 2) return Triple(0.0, 0, 0.0)

        var minDist = Double.MAX_VALUE
        var bestSegment = 0
        var bestOffset = 0.0

        for (i in 0 until points.size - 1) {
            val p1 = points[i]
            val p2 = points[i + 1]
            val (dist, offset) = pointToSegmentDistance(userPoint, p1, p2)
            if (dist < minDist) {
                minDist = dist
                bestSegment = i
                bestOffset = offset
            }
        }

        return Triple(minDist, bestSegment, bestOffset)
    }

    private fun pointToSegmentDistance(p: GeoPoint, a: GeoPoint, b: GeoPoint): Pair<Double, Double> {
        val dx = b.lon - a.lon
        val dy = b.lat - a.lat
        val lenSq = dx * dx + dy * dy

        if (lenSq == 0.0) {
            return Pair(distanceMeters(p, a), 0.0)
        }

        // Project onto segment
        val t = max(0.0, min(1.0, ((p.lon - a.lon) * dx + (p.lat - a.lat) * dy) / lenSq))
        val projLon = a.lon + t * dx
        val projLat = a.lat + t * dy
        val projPoint = GeoPoint(projLat, projLon)

        val dist = distanceMeters(p, projPoint)
        val offsetM = distanceMeters(a, projPoint)
        return Pair(dist, offsetM)
    }

    fun distanceMeters(p1: GeoPoint, p2: GeoPoint): Double {
        val r = 6371000.0 // Earth radius in meters
        val lat1Rad = Math.toRadians(p1.lat)
        val lat2Rad = Math.toRadians(p2.lat)
        val dLat = Math.toRadians(p2.lat - p1.lat)
        val dLon = Math.toRadians(p2.lon - p1.lon)

        val a = sin(dLat / 2).pow(2) +
                cos(lat1Rad) * cos(lat2Rad) *
                sin(dLon / 2).pow(2)
        val c = 2 * atan2(sqrt(a), sqrt(1 - a))
        return r * c
    }
}

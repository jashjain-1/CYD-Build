package com.dashcue.feature.navigation

data class GeoPoint(val lat: Double, val lon: Double)

object PolylineDecoder {
    private const val PRECISION = 1_000_000.0

    /**
     * Decodes an OSRM polyline6 string into a list of GeoPoints.
     */
    fun decodePolyline6(encoded: String): List<GeoPoint> {
        val poly = ArrayList<GeoPoint>()
        var index = 0
        val len = encoded.length
        var lat = 0
        var lng = 0

        while (index < len) {
            var b: Int
            var shift = 0
            var result = 0
            do {
                if (index >= len) break
                b = encoded[index++].code - 63
                result = result or ((b and 0x1f) shl shift)
                shift += 5
            } while (b >= 0x20)
            val dlat = if ((result and 1) != 0) (result shr 1).inv() else (result shr 1)
            lat += dlat

            shift = 0
            result = 0
            do {
                if (index >= len) break
                b = encoded[index++].code - 63
                result = result or ((b and 0x1f) shl shift)
                shift += 5
            } while (b >= 0x20)
            val dlng = if ((result and 1) != 0) (result shr 1).inv() else (result shr 1)
            lng += dlng

            poly.add(GeoPoint(lat / PRECISION, lng / PRECISION))
        }
        return poly
    }

    /**
     * Encodes a list of GeoPoints into a polyline6 string.
     */
    fun encodePolyline6(points: List<GeoPoint>): String {
        val str = StringBuilder()
        var prevLat = 0
        var prevLng = 0
        for (point in points) {
            val lat = Math.round(point.lat * PRECISION).toInt()
            val lng = Math.round(point.lon * PRECISION).toInt()
            encodeValue(lat - prevLat, str)
            encodeValue(lng - prevLng, str)
            prevLat = lat
            prevLng = lng
        }
        return str.toString()
    }

    private fun encodeValue(value: Int, str: StringBuilder) {
        var v = if (value < 0) (value shl 1).inv() else (value shl 1)
        while (v >= 0x20) {
            str.append(((0x20 or (v and 0x1f)) + 63).toChar())
            v = v shr 5
        }
        str.append((v + 63).toChar())
    }
}

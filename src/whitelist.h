// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <cstdint>

/*
 * Whitelist of allowed CAN identifiers for XCSoar telemetry
 * IMPORTANT: keep in sync with XCSoar, file src/Device/Driver/CANaerospace/canaerospace/message.h
 **/
typedef enum
{
    BODY_LONG_ACC_ID = 300, /* body longitudinal acceleration */
    BODY_LAT_ACC_ID = 301, /* body lateral acceleration */
    BODY_NORM_ACC_ID = 302, /* body normal acceleration */
    INDICATED_AIRSPEED = 315,
    TRUE_AIRSPEED = 316,
    BARO_CORRECTION_ID = 319, /* barometric correction (QNH) */
    HEADING_ANGLE = 321,
    STANDARD_ALTITUDE = 322,
    STATIC_PRESSURE = 326,
    WIND_SPEED_ID = 333, /* wind speed in [m/s] */
    WIND_DIRECTION_ID = 334, /* wind direction in degrees */
    AIRMASS_SPEED_VERTICAL = 354, /* Vertical speed of the airmass earth NED (negative is lift)*/
    OUTSIDE_AIR_TEMP_ID = 335, /* outside air temperature */

    GPS_AIRCRAFT_LATITUDE = 1036,
    GPS_AIRCRAFT_LONGITUDE = 1037,
    GPS_AIRCRAFT_HEIGHTABOVE_ELLIPSOID = 1038,
    GPS_GROUND_SPEED = 1039,
    GPS_TRUE_TRACK = 1040,
    UTC = 1200,

    FLARM_STATE_ID = 1300,
    FLARM_OBJECT_AL3_ID = 1301,
    FLARM_OBJECT_AL2_ID = 1302,
    FLARM_OBJECT_AL1_ID = 1303,
    FLARM_OBJECT_AL0_ID = 1304,

    ADSB_STATE_ID = 1305,

    VARIO_MODE_ID = 1510,
    MCCRADY_VALUE_ID = 1518,
    BARO_ALT_CORR_ID = 1519 /* barometric altitude corretion in meters: altQNH = altSTD + <value> */
    /* SC = 0: QNH  SC = 1: QFE */
} CanIDsForXCSoar;

bool is_whitelisted_id(uint16_t id);

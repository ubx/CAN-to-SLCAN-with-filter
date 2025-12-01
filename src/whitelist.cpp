// SPDX-License-Identifier: MIT
#include "whitelist.h"

// Always compile the actual whitelist here; optional bypass is handled in main.cpp
bool is_whitelisted_id(uint16_t id)
{
    switch (id)
    {
        case BODY_LONG_ACC_ID:
        case BODY_LAT_ACC_ID:
        case BODY_NORM_ACC_ID:
        case INDICATED_AIRSPEED:
        case TRUE_AIRSPEED:
        case BARO_CORRECTION_ID:
        case HEADING_ANGLE:
        case STANDARD_ALTITUDE:
        case STATIC_PRESSURE:
        case WIND_SPEED_ID:
        case WIND_DIRECTION_ID:
        case AIRMASS_SPEED_VERTICAL:
        case OUTSIDE_AIR_TEMP_ID:
        case GPS_AIRCRAFT_LATITUDE:
        case GPS_AIRCRAFT_LONGITUDE:
        case GPS_AIRCRAFT_HEIGHTABOVE_ELLIPSOID:
        case GPS_GROUND_SPEED:
        case GPS_TRUE_TRACK:
        case UTC:
        case FLARM_STATE_ID:
        case FLARM_OBJECT_AL3_ID:
        case FLARM_OBJECT_AL2_ID:
        case FLARM_OBJECT_AL1_ID:
        case FLARM_OBJECT_AL0_ID:
        case ADSB_STATE_ID:
        case VARIO_MODE_ID:
        case MCCRADY_VALUE_ID:
        case BARO_ALT_CORR_ID:
            return true;
        default:
            return false;
    }
}

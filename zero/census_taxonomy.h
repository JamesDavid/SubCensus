// ============================================================================
// GENERATED FILE — DO NOT EDIT.
// Source of truth: shared/taxonomy.yaml
// Regenerate: python -m subcensus_tools.codegen   (from tools/)
// A schema/taxonomy change lands in shared/ and this file regenerates in the
// same commit (System §10) — so the tools cannot drift.
// ============================================================================

#pragma once

#include <string.h>

#define CENSUS_TAXONOMY_VERSION 1

typedef enum {
    CENSUS_CLASS_GARAGE = 0,  // Garage door
    CENSUS_CLASS_CAR_FOB = 1,  // Car key fob
    CENSUS_CLASS_TPMS = 2,  // Tire pressure sensor
    CENSUS_CLASS_WEATHER = 3,  // Weather / environment sensor
    CENSUS_CLASS_DOORBELL = 4,  // Doorbell
    CENSUS_CLASS_PIR_MOTION = 5,  // PIR / motion sensor
    CENSUS_CLASS_ENERGY_METER = 6,  // Energy meter
    CENSUS_CLASS_WATER_GAS_METER = 7,  // Water / gas meter
    CENSUS_CLASS_REMOTE = 8,  // Remote control
    CENSUS_CLASS_THERMOSTAT = 9,  // Thermostat
    CENSUS_CLASS_SMART_HOME = 10,  // Smart-home device
    CENSUS_CLASS_BEACON = 11,  // Beacon
    CENSUS_CLASS_UNKNOWN = 12,  // Unknown
    CENSUS_CLASS_OTHER = 13,  // Other
    CENSUS_CLASS_COUNT = 14,
} CensusDeviceClass;

static inline const char* census_class_id(CensusDeviceClass c) {
    switch(c) {
        case CENSUS_CLASS_GARAGE: return "garage";
        case CENSUS_CLASS_CAR_FOB: return "car-fob";
        case CENSUS_CLASS_TPMS: return "tpms";
        case CENSUS_CLASS_WEATHER: return "weather";
        case CENSUS_CLASS_DOORBELL: return "doorbell";
        case CENSUS_CLASS_PIR_MOTION: return "pir-motion";
        case CENSUS_CLASS_ENERGY_METER: return "energy-meter";
        case CENSUS_CLASS_WATER_GAS_METER: return "water/gas-meter";
        case CENSUS_CLASS_REMOTE: return "remote";
        case CENSUS_CLASS_THERMOSTAT: return "thermostat";
        case CENSUS_CLASS_SMART_HOME: return "smart-home";
        case CENSUS_CLASS_BEACON: return "beacon";
        case CENSUS_CLASS_UNKNOWN: return "unknown";
        case CENSUS_CLASS_OTHER: return "other";
        default: return "unknown";
    }
}

static inline const char* census_class_name(CensusDeviceClass c) {
    switch(c) {
        case CENSUS_CLASS_GARAGE: return "Garage door";
        case CENSUS_CLASS_CAR_FOB: return "Car key fob";
        case CENSUS_CLASS_TPMS: return "Tire pressure sensor";
        case CENSUS_CLASS_WEATHER: return "Weather / environment sensor";
        case CENSUS_CLASS_DOORBELL: return "Doorbell";
        case CENSUS_CLASS_PIR_MOTION: return "PIR / motion sensor";
        case CENSUS_CLASS_ENERGY_METER: return "Energy meter";
        case CENSUS_CLASS_WATER_GAS_METER: return "Water / gas meter";
        case CENSUS_CLASS_REMOTE: return "Remote control";
        case CENSUS_CLASS_THERMOSTAT: return "Thermostat";
        case CENSUS_CLASS_SMART_HOME: return "Smart-home device";
        case CENSUS_CLASS_BEACON: return "Beacon";
        case CENSUS_CLASS_UNKNOWN: return "Unknown";
        case CENSUS_CLASS_OTHER: return "Other";
        default: return "Unknown";
    }
}

// Returns the CensusDeviceClass for an id string, or -1 if unknown.
static inline int census_class_from_id(const char* s) {
    if(!s) return -1;
    if(strcmp(s, "garage") == 0) return 0;
    if(strcmp(s, "car-fob") == 0) return 1;
    if(strcmp(s, "tpms") == 0) return 2;
    if(strcmp(s, "weather") == 0) return 3;
    if(strcmp(s, "doorbell") == 0) return 4;
    if(strcmp(s, "pir-motion") == 0) return 5;
    if(strcmp(s, "energy-meter") == 0) return 6;
    if(strcmp(s, "water/gas-meter") == 0) return 7;
    if(strcmp(s, "remote") == 0) return 8;
    if(strcmp(s, "thermostat") == 0) return 9;
    if(strcmp(s, "smart-home") == 0) return 10;
    if(strcmp(s, "beacon") == 0) return 11;
    if(strcmp(s, "unknown") == 0) return 12;
    if(strcmp(s, "other") == 0) return 13;
    return -1;
}

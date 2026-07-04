"""SubCensusPi — RTL-SDR / Raspberry Pi ISM census.

Shares the SubCensus data + intelligence layer (see ../../shared and SubCensus_System.md).
This package is the Pi-native realization: rtl_433 collector, SQLite catalog, FastAPI
dashboard, MQTT/HA discovery, plus `dsp/` — the Python port of shared/core, parity-locked
to the C golden fixtures.
"""

__version__ = "0.1.0"

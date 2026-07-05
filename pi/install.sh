#!/usr/bin/env bash
# SubCensusPi one-line installer (Raspberry Pi OS / Debian / Ubuntu).
#
#   curl -sSL https://raw.githubusercontent.com/JamesDavid/SubCensus/master/pi/install.sh | bash
#
# Installs the system deps (rtl-433 + RTL-SDR), clones the repo, creates a venv, installs the
# pi package, and seeds config.yaml. Override the target dir with SUBCENSUS_DIR=/path.
# RX-only; no radio is touched by the install — a dongle is only needed to receive.
set -euo pipefail

REPO_URL="https://github.com/JamesDavid/SubCensus.git"
DEST="${SUBCENSUS_DIR:-$HOME/SubCensus}"

echo "== SubCensusPi installer =="

# 1. system packages: rtl_433 + RTL-SDR + python toolchain
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    sudo apt-get install -y git python3 python3-venv python3-pip rtl-433 rtl-sdr librtlsdr-dev
else
    echo "!! non-apt system: install git, python3-venv, rtl-433 and librtlsdr yourself, then re-run."
    echo "   (RTL-SDR Blog v4 needs a current librtlsdr — build from source if apt's is old.)"
fi

# 2. clone (or fast-forward) the repo
if [ -d "$DEST/.git" ]; then
    echo "-- updating $DEST"
    git -C "$DEST" pull --ff-only
else
    echo "-- cloning into $DEST"
    git clone --depth 1 "$REPO_URL" "$DEST"
fi

# 3. venv + install the pi package
cd "$DEST/pi"
python3 -m venv .venv
# shellcheck disable=SC1091
. .venv/bin/activate
pip install --upgrade pip
pip install -e '.[dev]'

# 4. seed a config if there isn't one yet
[ -f config.yaml ] || cp config.example.yaml config.yaml

cat <<MSG

Done — SubCensusPi is installed at $DEST/pi

Next:
  cd $DEST/pi && . .venv/bin/activate
  \$EDITOR config.yaml            # dongle serial(s), place, signatures_dir, MQTT
  python -m subcensuspi.collector.main --config config.yaml                       # collector (needs a dongle)
  SUBCENSUSPI_DB=census.db uvicorn subcensuspi.web.app:app --host 0.0.0.0 --port 8080   # dashboard

No dongle yet? Drive the whole decode -> catalog -> dashboard path from a recorded fixture:
  python -m subcensuspi.collector.main --config config.example.yaml \\
      --replay ../test/fixtures/rtl433/home_stream.jsonl --db census.db

Run it for real under systemd (collector + web): see pi/README.md and the unit files in
pi/subcensuspi/systemd/ (subcensuspi-collector.service + subcensuspi-web.service).
MSG

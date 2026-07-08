#!/usr/bin/env bash
# SubCensusPi one-line installer (Raspberry Pi OS / Debian / Ubuntu).
#
#   curl -sSL https://raw.githubusercontent.com/JamesDavid/SubCensus/master/pi/install.sh | bash
#
# Installs the deps (rtl-433 + RTL-SDR), clones the repo, creates a venv, installs the pi
# package, provisions the data dir, seeds config.yaml, and starts the services. Safe to re-run:
# every step is idempotent (already-installed apt packages are skipped, the repo fast-forwards,
# the venv/pip/services just refresh). A FULL `apt upgrade` is opt-in (SUBCENSUS_UPGRADE=1) so
# re-runs are fast. Override the target dir with SUBCENSUS_DIR=/path.
# RX-only; no radio is touched by the install — a dongle is only needed to receive.
set -euo pipefail

REPO_URL="https://github.com/JamesDavid/SubCensus.git"
DEST="${SUBCENSUS_DIR:-$HOME/SubCensus}"
DATA_DIR="${SUBCENSUS_DATA_DIR:-/var/lib/subcensuspi}"   # matches config.example.yaml defaults

echo "== SubCensusPi installer =="

# 1. install packages: rtl_433 + RTL-SDR + python toolchain (idempotent — apt skips what's
#    already installed). A full system upgrade is OPT-IN so re-running the installer is fast.
if command -v apt-get >/dev/null 2>&1; then
    sudo apt-get update
    if [ "${SUBCENSUS_UPGRADE:-0}" = "1" ]; then
        echo "-- SUBCENSUS_UPGRADE=1: upgrading all installed packages (slow)"
        sudo DEBIAN_FRONTEND=noninteractive apt-get -y upgrade
    fi
    sudo apt-get install -y git python3 python3-venv python3-pip rtl-433 rtl-sdr librtlsdr-dev
    # let this user access the RTL-SDR over USB without root (udev rules ship with rtl-sdr).
    sudo usermod -aG plugdev "$USER" 2>/dev/null || true
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

# 4. provision the data dir (owned by the invoking user) so the default db_path / places_dir /
#    signatures_dir / iq_dir are writable for a manual run AND for the systemd services.
if command -v install >/dev/null 2>&1; then
    echo "-- provisioning $DATA_DIR (owned by $USER)"
    sudo install -d -o "$USER" -g "$(id -gn)" \
        "$DATA_DIR" "$DATA_DIR/places" "$DATA_DIR/signatures" "$DATA_DIR/iq"
fi

# 5. seed a config if there isn't one yet
[ -f config.yaml ] || cp config.example.yaml config.yaml

# 6. install + start the systemd services so the collector + dashboard auto-run on boot and
#    restart on crash (this is the default; opt out with SUBCENSUS_NO_SERVICE=1). The collector
#    tolerates a missing/idle dongle — its per-dongle supervisor just keeps retrying with backoff,
#    so the service stays up either way (Pi §9).
install_services() {
    echo "-- installing + starting systemd services (User=$USER, dir=$DEST/pi)"
    for unit in subcensuspi-collector subcensuspi-web; do
        sed -e "s|^User=pi\$|User=$USER|" \
            -e "s|/home/pi/SubCensus|$DEST|g" \
            "$DEST/pi/subcensuspi/systemd/$unit.service" |
            sudo tee "/etc/systemd/system/$unit.service" >/dev/null
    done
    sudo systemctl daemon-reload
    sudo systemctl enable --now subcensuspi-collector subcensuspi-web
    sleep 1
    sudo systemctl --no-pager --lines=0 status subcensuspi-collector || true
}
if command -v systemctl >/dev/null 2>&1 && [ "${SUBCENSUS_NO_SERVICE:-0}" != "1" ]; then
    install_services
    STARTED=1
else
    STARTED=0
fi

IP="$(hostname -I 2>/dev/null | awk '{print $1}')"
cat <<MSG

Done — SubCensusPi is installed at $DEST/pi
MSG
if [ "$STARTED" = "1" ]; then
cat <<MSG

The collector + dashboard are RUNNING as systemd services (auto-start on boot):
  dashboard   ->  http://${IP:-<pi-ip>}:8080
  live logs   ->  journalctl -u subcensuspi-collector -f
  status      ->  systemctl status subcensuspi-collector subcensuspi-web
  stop/disable->  sudo systemctl disable --now subcensuspi-collector subcensuspi-web

If SDR access needs the new 'plugdev' group membership, reboot (or re-plug the dongle) once:
  sudo reboot

Edit config.yaml (dongle serial / place / MQTT) then restart:
  \$EDITOR $DEST/pi/config.yaml && sudo systemctl restart subcensuspi-collector
MSG
else
cat <<MSG

Services were not started (no systemd, or SUBCENSUS_NO_SERVICE=1). Run in the foreground:
  cd $DEST/pi && . .venv/bin/activate
  python -m subcensuspi.collector.main --config config.yaml
  SUBCENSUSPI_DB=$DATA_DIR/census.db uvicorn subcensuspi.web.app:app --host 0.0.0.0 --port 8080
MSG
fi

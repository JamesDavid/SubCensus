#!/usr/bin/env bash
# SubCensusPi one-line installer (Raspberry Pi OS / Debian / Ubuntu).
#
#   curl -sSL https://raw.githubusercontent.com/JamesDavid/SubCensus/master/pi/install.sh | bash
#
# Installs the deps (rtl-433 + RTL-SDR), clones the repo, creates a venv, installs the pi
# package, provisions the data dir, seeds config.yaml, and starts the ONE service. Safe to re-run:
# every step is idempotent (already-installed apt packages are skipped, the repo fast-forwards,
# the venv/pip/service just refresh). A FULL `apt upgrade` is opt-in (SUBCENSUS_UPGRADE=1) so
# re-runs are fast. Override the target dir with SUBCENSUS_DIR=/path.
#
# ONE service (`subcensuspi`) runs the web dashboard, and the dashboard OWNS the single dongle:
# it switches the radio between off / decode (rtl_433 census) / spectrum (live rtl_power waterfall)
# from the Capture control — no separate collector service, so two processes never fight one radio.
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
    # THE classic RTL-SDR fix: the kernel DVB-T TV driver grabs the dongle, so rtl_433/rtl_power
    # get "usb_claim_interface error -6". Blacklist it (persists across reboots) and unload it now
    # so no reboot is needed.
    printf 'blacklist %s\n' dvb_usb_rtl28xxu rtl2832 rtl2830 rtl2838 rtl2832_sdr |
        sudo tee /etc/modprobe.d/blacklist-rtl-sdr.conf >/dev/null
    sudo modprobe -r dvb_usb_rtl28xxu 2>/dev/null || true
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

# 5. seed a config if there isn't one yet — both a local copy (for a manual foreground run) and
#    the one the service reads for decode mode ($DATA_DIR/config.yaml, per the systemd unit).
[ -f config.yaml ] || cp config.example.yaml config.yaml
[ -f "$DATA_DIR/config.yaml" ] || cp config.example.yaml "$DATA_DIR/config.yaml"

# 6. drop any pre-refactor two-service install so it can't keep fighting for the dongle.
if command -v systemctl >/dev/null 2>&1; then
    for old in subcensuspi-collector subcensuspi-web; do
        if [ -f "/etc/systemd/system/$old.service" ]; then
            echo "-- removing old service $old (folded into the single subcensuspi service)"
            sudo systemctl disable --now "$old" 2>/dev/null || true
            sudo rm -f "/etc/systemd/system/$old.service"
        fi
    done
fi

# 7. install + start the ONE systemd service so the dashboard (which owns the radio) auto-runs on
#    boot and restarts on crash (default; opt out with SUBCENSUS_NO_SERVICE=1). The last-selected
#    radio mode is persisted and re-applied on boot, so a headless Pi resumes decoding by itself.
#    Decode tolerates a missing/idle dongle — its per-dongle supervisor retries with backoff (§9).
install_services() {
    echo "-- installing + starting the subcensuspi service (User=$USER, dir=$DEST/pi)"
    sed -e "s|^User=pi\$|User=$USER|" \
        -e "s|/home/pi/SubCensus|$DEST|g" \
        "$DEST/pi/subcensuspi/systemd/subcensuspi.service" |
        sudo tee "/etc/systemd/system/subcensuspi.service" >/dev/null
    sudo systemctl daemon-reload
    sudo systemctl enable subcensuspi
    # restart (not just start) so re-running the installer picks up pulled code changes
    sudo systemctl restart subcensuspi
    sleep 1
    sudo systemctl --no-pager --lines=0 status subcensuspi || true
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

The SubCensusPi dashboard is RUNNING as one systemd service (auto-starts on boot):
  dashboard   ->  http://${IP:-<pi-ip>}:8080
  live logs   ->  journalctl -u subcensuspi -f
  status      ->  systemctl status subcensuspi

ONE service owns the ONE dongle. Pick what the radio does from the dashboard's Capture control:
  Off        - radio idle, dongle free (e.g. for 'rtl_test -t')
  Decode     - the rtl_433 census (the 24/7 default; writes the device catalog + MQTT/HA)
  Spectrum   - continuous live rtl_power waterfall on the chosen band (315/433/ISM/…)
They are mutually exclusive (one radio) — switching modes stops the other automatically. The
selected mode is remembered and resumes on reboot.

Check the dongle is claimable (set Capture to Off first; should say "Found 1... Sampling", NO -6):
  rtl_test -t

Edit the decode config (dongle serial / place / MQTT) then restart:
  \$EDITOR $DATA_DIR/config.yaml && sudo systemctl restart subcensuspi
MSG
else
cat <<MSG

The service was not started (no systemd, or SUBCENSUS_NO_SERVICE=1). Run the dashboard yourself
(it owns the radio; switch decode/spectrum from the Capture control in the browser):
  cd $DEST/pi && . .venv/bin/activate
  SUBCENSUSPI_DB=$DATA_DIR/census.db \\
    SUBCENSUSPI_PLACES_DIR=$DATA_DIR/places \\
    SUBCENSUSPI_CONFIG=$DATA_DIR/config.yaml \\
    SUBCENSUSPI_RADIO_STATE=$DATA_DIR/radio_state.json \\
    uvicorn subcensuspi.web.app:app --host 0.0.0.0 --port 8080
MSG
fi

#!/bin/bash

INSTALL_DIR="/usr/local/bin"

download_executable() {
    local url=$1
    local filepath="${INSTALL_DIR}/$(basename $url)"
    if [ ! -f "$filepath" ]; then
        echo "Downloading $filepath..."
        wget -q -O "$filepath" "$url"
        chmod +x "$filepath"
    else
        echo "$filepath already exists."
    fi
}

download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v3.0-pre/gddr6"
download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v3.0-pre/metrics_exporter"

cleanup() {
  echo "Stopping gddr6 and metrics_exporter..."
  pkill -P $$  # Kills all child processes of the script
  exit
}

trap cleanup SIGTERM SIGINT

run_gddr6() {
  while true; do
    "${INSTALL_DIR}/gddr6"
    echo "gddr6 crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

run_metrics_exporter() {
  while true; do
    "${INSTALL_DIR}/metrics_exporter"
    echo "metrics_exporter crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

run_gddr6 &
run_metrics_exporter &

wait

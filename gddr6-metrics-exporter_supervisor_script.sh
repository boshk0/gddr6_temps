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

download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v0.9/nvml_direct_access"
download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v0.9/metrics_exporter"
download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v0.9/metrics.ini"

cleanup() {
  echo "Stopping gddr6 and metrics_exporter..."
  pkill -P $$  # Kills all child processes of the script
  exit
}

trap cleanup SIGTERM SIGINT

run_nvml_direct_access() {
  while true; do
    "${INSTALL_DIR}/nvml_direct_access"
    echo "nvml_direct_access crashed with exit code $?. Respawning.." >&2
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

run_nvml_direct_access &
run_metrics_exporter &

wait

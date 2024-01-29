#!/bin/bash

# Define the directory where the executables will be stored
INSTALL_DIR="/usr/local/bin"

# Function to download and prepare the executable
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

# Download gddr6 and metrics_exporter
download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v2.0-pre/gddr6"
download_executable "https://github.com/jjziets/gddr6_temps/releases/download/v2.0-pre/metrics_exporter"

# Function to start gddr6 and restart it if it exits
run_gddr6() {
  while true; do
    "${INSTALL_DIR}/gddr6"
    echo "gddr6 crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

# Function to start metrics_exporter and restart it if it exits
run_metrics_exporter() {
  while true; do
    "${INSTALL_DIR}/metrics_exporter"
    echo "metrics_exporter crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

# Start both processes in the background
run_gddr6 &
run_metrics_exporter &

# Wait for all background processes to finish
wait

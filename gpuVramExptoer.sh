#!/bin/bash

# Function to start gddr6 and restart it if it exits
run_nvml_direct_access() {
  while true; do
    ./nvml_direct_access
    echo "nvml_direct_access crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

# Function to start metrics_exporter and restart it if it exits
run_metrics_exporter() {
  while true; do
    ./metrics_exporter
    echo "metrics_exporter crashed with exit code $?. Respawning.." >&2
    sleep 1
  done
}

# Start both processes in the background
run_nvml_direct_access &
run_metrics_exporter &

# Wait for all background processes to finish
wait

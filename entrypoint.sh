#!/bin/bash

# Function to start nvml_direct_access and restart it if it exits
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

# Cleanup function to be executed when the script receives a signal to stop
cleanup() {
  echo "Stopping nvml_direct_access and metrics_exporter..."
  pkill -P $$  # Kills all child processes of the script
  exit
}

# Trap TERM and INT signals and call the cleanup function
trap cleanup SIGTERM SIGINT

# Start both processes in the background
run_nvml_direct_access &
run_metrics_exporter &

# Wait for all background processes to finish
wait

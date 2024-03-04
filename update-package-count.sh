#!/bin/bash

# Define the output file path
OUTPUT_FILE="/var/log/package-count.txt"

# Function to check and count upgradable packages
countUpgradable() {
    if [ -f "$OUTPUT_FILE" ]; then
        # If running inside a container and file exists, read the count
        cat "$OUTPUT_FILE" | while IFS= read -r line
        do
            echo "$line"
        done
    else
        # Assume running on host or file doesn't exist, count packages
        sudo apt-get update > /dev/null 2>&1
        UPGRADABLE_COUNT=$(apt list --upgradable 2>/dev/null | grep -c 'upgradable from')
        echo "Upgradable packages: $UPGRADABLE_COUNT" > "$OUTPUT_FILE"
    fi
}

# Execute the function
countUpgradable

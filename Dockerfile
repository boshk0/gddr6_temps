# Use the CUDA 12.1.0 devel Ubuntu 20.04 image as a base
FROM nvidia/cuda:12.1.0-devel-ubuntu20.04

# Set the working directory in the container
WORKDIR /usr/src/app

# Install necessary packages for building
RUN apt-get update && apt-get install -y \
    build-essential \
    pciutils \
    libpci-dev


# Copy the necessary files into the container
COPY gddr6.c .
COPY metrics_exporter.cpp .
COPY httplib.h .
COPY entrypoint.sh .

# Build the gddr6 application
RUN gcc -std=c11 -O3 -Wall -I/usr/local/cuda/include -o gddr6 gddr6.c -lpci -lnvidia-ml

# Build the metrics_exporter application
RUN g++ -std=c++11 -o metrics_exporter metrics_exporter.cpp -lpthread

# Expose port 9500 to the host
EXPOSE 9500

# Set the entrypoint script
ENTRYPOINT ["./entrypoint.sh"]

## GDDR6/GDDR6X GPU Memory Temperature Reader for Linux with Prometheus expoter. it forms part of my bigger project https://github.com/jjziets/DCMontoring/tree/main

This program reads GDDR6/GDDR6X VRAM memory temperatures from multiple supported NVIDIA GPUs found in a host Linux system and creates a Prometheus exporter that allows Prometheus to scrape it on port 9500.

**Installation**
Docker:
You can run the exporter as a Docker container. Ensure you have Docker installed, and then execute the following command:
```
docker run -d --privileged --gpus all -p 9500:9500 jjziets/gddr6-metrics-exporter:latest

```

Service (Linux Machine):
You can also run it as a service on a Linux machine:
```
bash -c "\
sudo wget -q -O /usr/local/bin/gddr6-metrics-exporter_supervisor_script.sh https://raw.githubusercontent.com/jjziets/gddr6_temps/master/gddr6-metrics-exporter_supervisor_script.sh && \
sudo chmod +x /usr/local/bin/gddr6-metrics-exporter_supervisor_script.sh && \
sudo wget -q -O /etc/systemd/system/gddr6-metrics-exporter.service https://raw.githubusercontent.com/jjziets/gddr6_temps/master/gddr6-metrics-exporter.service && \
sudo systemctl daemon-reload && \
sudo systemctl enable gddr6-metrics-exporter && \
sudo systemctl start gddr6-metrics-exporter"
```
Check if it's running with:
```
sudo systemctl status gddr6-metrics-exporter
```

Program:
If you prefer running it as a standalone program, follow these steps:

## Dependencies
- libpci-dev 
```
sudo apt install libpci-dev -y
```

- Kernel boot parameter: iomem=relaxed
```
sudo vim /etc/default/grub
GRUB_CMDLINE_LINUX_DEFAULT="quiet splash iomem=relaxed"
sudo update-grub
sudo reboot
```

## Clone & Run
```
sudo git clone https://github.com/jjziets/gddr6_temps.git
sudo make && sudo ./nvml_direct_access &

sudo g++ -std=c++11 -o metrics_exporter metrics_exporter.cpp -lpthread
sudo chmod +x metrics_exporter
sudo metrics_exporter &
```

nvml_direct_access will write to the local storage metrics.txt 
metrics_exporter read this metrics.txt and provide a basic website that can be scraped by Prometheus. 

## Supported GPUs
- RTX A6000 (AD102)
- RTX A5000
- RTX 4090 (AD102)
- RTX 4080 (AD103)
- RTX 4070 Ti (AD104)
- RTX 4070 (AD104)
- RTX 3090 (GA102)
- RTX 3080 Ti (GA102)
- RTX 3080 (GA102)
- RTX 3080 LHR (GA102)
- RTX A2000 (GA106)
- RTX A4500 (GA102)
- L4 (AD104)


Credit goes to olealgoritme/gddr6 for his work in using the Pcie bus direct read to get to the undocumented registers.  also, a shout out to ddobreff for [mmpos](https://app.mmpos.eu/) for helping find the hotspot register. 

## GDDR6/GDDR6X GPU Memory Temperature Reader for Linux with Prometheus expoter. it forms part of my bigger project https://github.com/jjziets/DCMontoring/tree/main

The program Reads GDDR6/GDDR6X VRAM memory temperatures from multiple supported NVIDIA GPUs found in a host Linux system.

It creates an exporter that allows Prometheus to scrape it on port 9500

There are several ways to run this

As a docker container 
```
docker run -d --privileged --gpus all -p 9500:9500 jjziets/gddr6-metrics-exporter:latest

```

As service one machine
```
bash -c "\
sudo wget -q -O /usr/local/bin/gddr6-metrics-exporter_supervisor_script.sh https://raw.githubusercontent.com/jjziets/gddr6_temps/master/gddr6-metrics-exporter_supervisor_script.sh && \
sudo chmod +x /usr/local/bin/gddr6-metrics-exporter_supervisor_script.sh && \
sudo wget -q -O /etc/systemd/system/gddr6-metrics-exporter.service https://raw.githubusercontent.com/jjziets/gddr6_temps/master/gddr6-metrics-exporter.service && \
sudo systemctl daemon-reload && \
sudo systemctl enable gddr6-metrics-exporter && \
sudo systemctl start gddr6-metrics-exporter"
```
Check that is running with sudo systemctl status gddr6-metrics-exporter

Or as a program. 
These findings are based on reverse engineering of the NVIDIA GPU Linux driver.

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
sudo make && sudo ./gddr6 &

sudo g++ -std=c++11 -o metrics_exporter metrics_exporter.cpp -lpthread
sudo chmod +x metrics_exporter
sudo metrics_exporter &
```

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
- RTX 3070 (GA104)
- RTX 3070 LHR (GA104)
- RTX A2000 (GA106)
- RTX A4500 (GA102)
- L4 (AD104)

![](https://github.com/olealgoritme/gddr6/blob/master/gddr6_use.gif)

#!/bin/bash

# RTL SDR FM Streamer Autodeploy Script for Ubuntu 24.04 (No Docker)

# Update system
echo "Updating system..."
sudo apt-get update && sudo apt-get upgrade -y

# Install required dependencies including dialog for interactive input
echo "Installing dependencies..."
sudo apt-get install -y git cmake build-essential libusb-1.0-0-dev libev-dev net-tools dialog

# Clone the fm2ip-streamer repository
echo "Cloning fm2ip-streamer repository..."
git clone https://github.com/mrgs83/fm2ip-streamer.git

# Build fm2ip-streamer
echo "Building fm2ip-streamer..."
cd fm2ip-streamer
mkdir -p build
cd build
cmake ../
make

# Add /usr/local/lib to the library path if it's not there already
if ! grep -q "/usr/local/lib" /etc/ld.so.conf.d/rtlsdr.conf; then
  echo "/usr/local/lib" | sudo tee /etc/ld.so.conf.d/rtlsdr.conf
  sudo ldconfig
fi

# Blacklist the conflicting DVB module to avoid conflicts with the RTL2832U device
echo "Blacklisting dvb_usb_rtl28xxu kernel module..."
echo "blacklist dvb_usb_rtl28xxu" | sudo tee /etc/modprobe.d/blacklist-rtl-sdr.conf
sudo rmmod dvb_usb_rtl28xxu

# Create systemd service for the tuner on port 8001
echo "Creating systemd service for the RTL SDR FM Streamer on port 8001..."

sudo tee /etc/systemd/system/rtl_fm_streamer.service > /dev/null <<EOL
[Unit]
Description=RTL SDR FM Streamer
After=network.target

[Service]
ExecStart=/usr/local/bin/rtl_fm_streamer -P 8001 -d 0
WorkingDirectory=/usr/local/bin
Restart=always
User=$(whoami)

[Install]
WantedBy=multi-user.target
EOL

# Enable and start the service
sudo systemctl daemon-reload
sudo systemctl enable rtl_fm_streamer.service
sudo systemctl start rtl_fm_streamer.service

# Display usage instructions
echo "RTL SDR FM Streamer is now running on port 8001:"
echo "  Mono: http://<your_ip>:8001/<FrequencyInHz>"
echo "  Stereo: http://<your_ip>:8001/<FrequencyInHz>/1"

# Installation complete
echo "Installation complete!"

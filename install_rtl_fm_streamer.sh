#!/bin/bash

# RTL SDR FM Streamer Autodeploy Script for Ubuntu 24.04 (No Docker)

# Update system
echo "Updating system..."
sudo apt-get update && sudo apt-get upgrade -y

# Install required dependencies including dialog for interactive input
echo "Installing dependencies..."
sudo apt-get install -y git cmake build-essential libusb-1.0-0-dev libev-dev net-tools dialog

# Use dialog to ask the user how many tuners to deploy
tuner_count=$(dialog --inputbox "How many tuners are you planning to use?" 10 30 3>&1 1>&2 2>&3 3>&-)
clear

# Validate tuner count input
if ! [[ "$tuner_count" =~ ^[0-9]+$ ]]; then
  echo "Invalid input. Please enter a number."
  exit 1
fi

# Clone the fm2ip-streamer repository
echo "Cloning fm2ip-streamer repository..."
git clone https://github.com/mrgs83/fm2ip-streamer.git

# Build fm2ip-streamer
echo "Building fm2ip-streamer..."
cd fm2ip-streamer
mkdir build
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

# Create systemd services for each tuner
for (( i=0; i<tuner_count; i++ )); do
  port=$((1000 + i))
  echo "Creating systemd service for tuner $i on port $port..."

  sudo tee /etc/systemd/system/rtl_fm_streamer_$i.service > /dev/null <<EOL
[Unit]
Description=RTL SDR FM Streamer for Tuner $i
After=network.target

[Service]
ExecStart=/usr/local/bin/rtl_fm_streamer -P $port -d $i
WorkingDirectory=/usr/local/bin
Restart=always
User=$(whoami)

[Install]
WantedBy=multi-user.target
EOL

  # Enable and start the service
  sudo systemctl daemon-reload
  sudo systemctl enable rtl_fm_streamer_$i.service
  sudo systemctl start rtl_fm_streamer_$i.service
done

# Display usage instructions
echo "RTL SDR FM Streamer is now running for $tuner_count tuners."
for (( i=0; i<tuner_count; i++ )); do
  port=$((1000 + i))
  echo "Tuner $i is streaming on port $port:"
  echo "  Mono: http://<your_ip>:$port/<FrequencyInHz>"
  echo "  Stereo: http://<your_ip>:$port/<FrequencyInHz>/1"
done

# Installation complete
echo "Installation complete!"

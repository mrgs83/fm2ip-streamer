#!/bin/bash

# RTL SDR FM Streamer Docker Autodeploy Script for Ubuntu 24.04

# Update system
echo "Updating system..."
sudo apt-get update && sudo apt-get upgrade -y

# Install required Docker dependencies
echo "Installing Docker dependencies..."
sudo apt-get install -y apt-transport-https ca-certificates curl software-properties-common

# Add Docker GPG key
echo "Adding Docker GPG key..."
curl -fsSL https://download.docker.com/linux/ubuntu/gpg | sudo apt-key add -

# Add Docker repository
echo "Adding Docker repository..."
sudo add-apt-repository "deb [arch=amd64] https://download.docker.com/linux/ubuntu $(lsb_release -cs) stable"

# Install Docker
echo "Installing Docker..."
sudo apt-get update
sudo apt-get install -y docker-ce docker-compose

# Enable Docker to start on boot
echo "Enabling Docker to start on boot..."
sudo systemctl enable docker
sudo systemctl start docker

# Configure network in bridge mode
echo "Configuring Docker network to bridge mode..."
docker network create -d bridge rtl_sdr_network

# Use dialog to ask the user how many containers to deploy
container_count=$(dialog --inputbox "How many containers are you planning to deploy?" 10 30 3>&1 1>&2 2>&3 3>&-)
clear

# Validate container count input
if ! [[ "$container_count" =~ ^[0-9]+$ ]]; then
  echo "Invalid input. Please enter a valid number."
  exit 1
fi

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

# Loop through the number of containers, configuring each one
for (( i=0; i<container_count; i++ )); do
  # Docker container name and port
  container_name="rtl_fm_streamer_$i"
  port=$((1000 + i))

  # Prompt the user to select the RTL device for each container
  rtl_device=$(dialog --inputbox "Enter the RTL-SDR device path for container $i (e.g., /dev/bus/usb/001/002):" 10 50 3>&1 1>&2 2>&3 3>&-)
  clear

  # Create and run Docker containers
  echo "Creating and starting container $container_name for tuner $i..."
  docker run -d \
    --name $container_name \
    --network rtl_sdr_network \
    --device $rtl_device \
    -p $port:1000 \
    -v $(pwd)/fm2ip-streamer/build:/usr/local/bin \
    ubuntu:24.04 /usr/local/bin/rtl_fm_streamer -P 1000 -d $i

  # Enable and start the service
  sudo systemctl daemon-reload
  sudo systemctl enable rtl_fm_streamer_$i.service
  sudo systemctl start rtl_fm_streamer_$i.service
done

# Display usage instructions
echo "RTL SDR FM Streamer is now running for $container_count containers."
for (( i=0; i<container_count; i++ )); do
  port=$((1000 + i))
  echo "Container $i is streaming on port $port:"
  echo "  Mono: http://<your_ip>:$port/<FrequencyInHz>"
  echo "  Stereo: http://<your_ip>:$port/<FrequencyInHz>/1"
done

# Installation complete
echo "Installation complete!"

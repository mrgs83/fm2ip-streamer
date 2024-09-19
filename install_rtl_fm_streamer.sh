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

# Loop through the number of containers, configuring each one
for i in "${!freqs[@]}"; do
  freq_hz=$(echo "${freqs[$i]} * 100000" ->

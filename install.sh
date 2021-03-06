#!/bin/bash
set -e

if ! which sudo >/dev/null; then
  apt update
  apt install -y sudo
fi

sudo apt update
sudo apt install -y apt-transport-https gnupg wget

wget -O - https://about.blaok.me/tapa/tapa.gpg.key | sudo apt-key add -
wget -O - https://about.blaok.me/fpga-runtime/frt.gpg.key | sudo apt-key add -

codename="$(grep --perl --only '(?<=UBUNTU_CODENAME=).+' /etc/os-release)"

# get pip
if test "${codename}" = "xenial"; then
  sudo apt install -y software-properties-common
  sudo add-apt-repository -y ppa:deadsnakes/ppa
  sudo apt update
  sudo apt install -y python3.6 python3-pip
  pip="python3.6 -m pip"
else
  sudo apt install -y python3 python3-pip
  pip="python3 -m pip"
fi

sudo tee /etc/apt/sources.list.d/tapa.list <<EOF
deb [arch=amd64] https://about.blaok.me/tapa ${codename} main
EOF
sudo tee /etc/apt/sources.list.d/frt.list <<EOF
deb [arch=amd64] https://about.blaok.me/fpga-runtime ${codename} main
EOF

sudo apt update
sudo apt install -y xrt --no-install-recommends || true
sudo apt install -y tapa
${pip} install --upgrade setuptools
${pip} install --user --upgrade tapa

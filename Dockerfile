FROM --platform=amd64 archlinux:latest
RUN pacman -Syyu base-devel --noconfirm
RUN pacman -Syyu arm-none-eabi-gcc --noconfirm
RUN pacman -Syyu arm-none-eabi-newlib --noconfirm
RUN pacman -Syyu git --noconfirm
RUN pacman -Syyu python python-pip --noconfirm
RUN pacman -Syyu python-crcmod --noconfirm
# Install GitHub CLI so container builds can auto-upload releases when credentials are provided
RUN pacman -Syyu gh --noconfirm || true
WORKDIR /app
COPY . .

# Skip submodule init inside container build (source tree already copied)
RUN echo "Skipping git submodule init in container build"
#RUN make && cp firmware* compiled-firmware/

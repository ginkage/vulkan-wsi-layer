ARG IMAGE
FROM ${IMAGE}

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y software-properties-common

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    debhelper \
    devscripts \
    cmake \
    pkg-config \
    git \
    libdrm-dev \
    libx11-dev \
    libx11-xcb-dev \
    libxcb1-dev \
    libxcb-dri3-dev \
    libxcb-randr0-dev \
    libxcb-shm0-dev \
    libxcb-sync-dev \
    libxrandr-dev \
    libvulkan-dev \
    libwayland-bin \
    libwayland-client0 \
    libwayland-dev \
    wayland-protocols \
 && apt-get clean autoclean --yes \
 && apt-get autoremove --yes \
 && rm -rf /var/cache/apt/archives* /var/lib/apt/lists/*

# Current upstream needs Vulkan headers >= 1.4.335, newer than the distro's libvulkan-dev.
RUN git clone --depth 1 --branch v1.4.335 \
    https://github.com/KhronosGroup/Vulkan-Headers.git /opt/vulkan-headers
ENV VULKAN_CXX_INCLUDE=/opt/vulkan-headers/include

COPY . /src/

WORKDIR /src/

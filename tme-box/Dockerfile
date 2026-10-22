ARG UBUNTU_VERSION=20.04

FROM ubuntu:${UBUNTU_VERSION} AS build-base

RUN sed -i 's/# deb-src/deb-src/' /etc/apt/sources.list
RUN ln -fs /use/share/zoneinfo/Europe/Berlin /etc/localtime

RUN DEBIAN_FRONTEND=noninteractive apt update
RUN DEBIAN_FRONTEND=noninteractive apt build-dep -y linux
# Install pahole for Ubuntu 22.04 and later
RUN DEBIAN_FRONTEND=noninteractive apt install -y --allow-downgrades \
    bc \
    gdb \
    git \
    dkms \
    flex \
    gawk \
    llvm \
    nasm \
    sudo \
    wget \
    zstd \
    bison \
    clang \
    cmake \
    dwarves \
    openssl \
    autoconf \
    fakeroot \
    keyutils \
    patchelf \
    xz-utils \
    msr-tools \
    pkg-config \
    python3-pip \
    cgroup-tools \
    build-essential \
    debhelper-compat \
    libdw-dev \
    libbsd-dev \
    libelf-dev \
    libpci-dev \
    libssl-dev \
    libpfm4-dev \
    libudev-dev \
    libiberty-dev \
    libncurses-dev \
    libkeyutils-dev \
    linux-tools-common
#    pahole=1.22-8
RUN rm -rf /var/lib/apt/lists/*


FROM ubuntu:${UBUNTU_VERSION}
ARG UNAME=user
ARG UID=1001
ARG GID=1001
COPY --from=build-base / /

RUN groupadd -g $GID -o $UNAME
RUN useradd -m -u ${UID} -g ${GID} -o -s /bin/bash ${UNAME}
RUN echo "${UNAME}:pass" | chpasswd
RUN usermod -aG sudo ${UNAME}
USER ${UNAME}
WORKDIR /home/${UNAME}/tme-box/

RUN git config --global --add safe.directory /home/${UNAME}

ENTRYPOINT [ "/bin/bash", "build.sh" ]

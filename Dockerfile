# Yocto build environment for epp-v2 (based on nxp-imx/imx-docker)
ARG UBUNTU=24.04
FROM ubuntu:${UBUNTU}

# Use DEBIAN_FRONTEND=noninteractive to avoid hanging on [Y/n] prompts.
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    gawk wget git-core diffstat unzip texinfo \
    gcc-multilib build-essential chrpath socat file cpio python3 \
    python3-pip python3-pexpect xz-utils debianutils iputils-ping \
    libsdl1.2-dev xterm tar locales net-tools rsync sudo vim curl zstd \
    liblz4-tool libssl-dev bc lzop libgnutls28-dev efitools git-lfs \
    openssl && \
    rm -rf /var/lib/apt/lists/*

# Set up locales
RUN locale-gen en_US.UTF-8 && \
    update-locale LC_ALL=en_US.UTF-8 LANG=en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8

# Yocto needs 'source', so make /bin/sh bash instead of dash.
RUN rm /bin/sh && ln -s bash /bin/sh

# Install repo
ADD https://storage.googleapis.com/git-repo-downloads/repo /usr/local/bin/
RUN chmod 755 /usr/local/bin/repo

# Add the host user to sudoers to be able to install packages in the container.
ARG USER
RUN echo "${USER} ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/${USER} && \
    chmod 0440 /etc/sudoers.d/${USER}

# Create a user with the host uid/gid so build artifacts on the mounted volumes
# are owned by the host user. Ubuntu 24.04 ships a default 'ubuntu' user with
# uid 1000, which would clash.
ARG host_uid \
    host_gid
RUN (userdel -r ubuntu 2>/dev/null || true) && \
    groupadd -g $host_gid nxp && \
    useradd -g $host_gid -m -s /bin/bash -u $host_uid $USER

# Yocto builds must run as a normal user.
USER $USER

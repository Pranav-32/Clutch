# Shared development environment for consistent builds and tests across machines.
# Project sources are mounted at runtime.
# The major LLVM version matches the repository's clang-format configuration.
ARG BASE_IMAGE=ubuntu:26.04

FROM ${BASE_IMAGE}

# Install image packages as root; interactive development uses clutch-dev below.
USER root
# Fail package pipelines when any stage fails, including key-download commands.
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# GCC remains available for compatibility checks. The desktop libraries support
# the CLion remote backend; no graphical desktop or Docker daemon is installed.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates cmake curl fontconfig g++ gcc gdb git gnupg graphviz \
        libfreetype6 libxi6 libxext6 libxrender1 libxtst6 make ninja-build \
        procps python3 sudo unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Verify LLVM's published signing key before adding a repository-scoped keyring.
# Source: https://apt.llvm.org/ (Ubuntu LLVM 22 repositories and key fingerprint).
# Download the key, check its fingerprint, register the matching Ubuntu repository,
# install the matching compiler/editor tools. Remove temporary package metadata.
RUN set -eux; \
    . /etc/os-release; \
    test "$ID" = ubuntu; \
    test -n "$VERSION_CODENAME"; \
    curl --fail --silent --show-error --location \
        https://apt.llvm.org/llvm-snapshot.gpg.key \
        --output /tmp/llvm-snapshot.gpg.key; \
    fingerprint="$(gpg --batch --show-keys --with-colons /tmp/llvm-snapshot.gpg.key \
        | awk -F: '$1 == "fpr" { print $10; exit }')"; \
    test "$fingerprint" = "6084F3CF814B57C1CF12EFD515CF4D18AF4F7421"; \
    gpg --batch --dearmor --output /usr/share/keyrings/llvm-archive-keyring.gpg \
        /tmp/llvm-snapshot.gpg.key; \
    printf '%s\n' \
        "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] https://apt.llvm.org/${VERSION_CODENAME}/ llvm-toolchain-${VERSION_CODENAME}-22 main" \
        > /etc/apt/sources.list.d/llvm-22.list; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        clang-22 clang-format-22 clang-tidy-22 clangd-22 lld-22; \
    rm -f /tmp/llvm-snapshot.gpg.key; \
    rm -rf /var/lib/apt/lists/*

# Explicit links keep command names stable without altering GCC's cc/c++ aliases.
RUN ln -s /usr/bin/clang-22 /usr/local/bin/clang \
    && ln -s /usr/bin/clang++-22 /usr/local/bin/clang++ \
    && ln -s /usr/bin/clang-format-22 /usr/local/bin/clang-format \
    && ln -s /usr/bin/clang-tidy-22 /usr/local/bin/clang-tidy \
    && ln -s /usr/bin/clangd-22 /usr/local/bin/clangd

# Use matching Clang C/C++ defaults; GCC remains available for explicit selection.
ENV CC=clang CXX=clang++

# Ubuntu may already supply a normal UID 1000 account. Reuse its UID/home rather
# than creating a conflicting second account; otherwise create the standard user.
# Passwordless sudo permits installing development tools from the attached shell.
RUN set -eux; \
    if getent passwd 1000 > /dev/null; then \
        original_user="$(getent passwd 1000 | cut -d: -f1)"; \
        usermod --login clutch-dev --home /home/clutch-dev --move-home "$original_user"; \
    else \
        useradd --uid 1000 --create-home --shell /bin/bash clutch-dev; \
    fi; \
    usermod --shell /bin/bash clutch-dev; \
    printf '%s\n' 'clutch-dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/clutch-dev; \
    chmod 0440 /etc/sudoers.d/clutch-dev; \
    mkdir -p /home/clutch-dev/workspace; \
    chown clutch-dev /home/clutch-dev/workspace

# Provide GitHub's CLI for repository authentication and contribution workflows.
# Keep this layer separate so adding/updating it reuses the compiler image layers.
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# The dev-container client adjusts this account's UID on Linux hosts when needed.
USER clutch-dev
WORKDIR /home/clutch-dev/workspace

# Keep the workspace available for IDE attachment and interactive build/test runs.
CMD ["sleep", "infinity"]

# syntax=docker/dockerfile:1
# Shared tools image. Sources are bind-mounted when the container starts.
ARG BASE_IMAGE=ubuntu:26.04
FROM ${BASE_IMAGE}

# Match the formatter generation required by .clang-format. All LLVM package
# names and stable command links derive from this one version.
ARG LLVM_VERSION=22
# Pin LLVM's published repository signing-key identity; review key rotations.
ARG LLVM_SIGNING_FINGERPRINT=6084F3CF814B57C1CF12EFD515CF4D18AF4F7421
ARG USERNAME=clutch-dev
ARG USER_UID=1000

USER root
SHELL ["/bin/bash", "-o", "pipefail", "-c"]

# Bind only installation inputs, keeping sources and helper scripts out of the
# image layers. Docker rebuilds this step when packages or installers change.
RUN --mount=type=bind,source=install-packages.sh,target=/tmp/install-packages.sh \
    --mount=type=bind,source=packages.txt,target=/tmp/packages.txt \
    bash /tmp/install-packages.sh "${LLVM_VERSION}" "${LLVM_SIGNING_FINGERPRINT}"

# Reuse a base-image account with the requested UID, preserving its home files.
RUN --mount=type=bind,source=create-user.sh,target=/tmp/create-user.sh \
    bash /tmp/create-user.sh "${USERNAME}" "${USER_UID}"

ENV CC=clang CXX=clang++
USER ${USERNAME}
WORKDIR /home/${USERNAME}/workspace

# Keep the container available for IDE attachment and terminal build/test runs.
CMD ["sleep", "infinity"]

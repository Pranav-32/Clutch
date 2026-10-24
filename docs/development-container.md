# Development container

Audience: Contributors using the shared Linux development environment.

The checked-in container provides a shared Ubuntu development environment on
Linux, macOS and Windows hosts. It includes CMake, Ninja, GNU Make, GCC, LLVM 22,
Clang's sanitizer runtimes, Graphviz, Git, GitHub CLI, debugging tools and Python
for tests. Windows and
macOS generators still require their native hosts.

Install Git and Docker with a running Linux container engine. Clone your fork,
then run `docker version` to confirm both the client and server are available.

## Open in an IDE

- **VS Code:** install Dev Containers, open the checkout and run
  **Dev Containers: Reopen in Container**.
- **CLion:** open `.devcontainer/devcontainer.json` and use
  **Create Dev Container and Mount Sources**, then connect to its backend.

The display name is **Clutch Development Container**; the Docker name is
`clutch-development-container`. The fixed Docker name must be available on the
engine. Use a distinct name for simultaneous checkouts.

The checkout is bind-mounted at `/home/clutch-dev/workspace`; edits and generated
files are shared with the host. The image contains tools, not a copy of the
repository. Launching the Dockerfile alone does not apply the source mount or
devcontainer lifecycle settings.

Initialization runs `.devcontainer/post-create.sh`, which configures a Clang
CMake/Ninja build in `build-container`, exports its compilation database and
runs the native test suites. VS Code's clangd settings use that database. In
CLion, select Ninja, `/usr/local/bin/clang`, `/usr/local/bin/clang++` and the
`build-container` CMake directory. Keep native builds in separate directories.

## Build and test

From the container's workspace terminal:

```sh
cmake --build build-container --parallel 2
ctest --test-dir build-container --output-on-failure
./build-container/clutch --version
./build-container/clutch examples/workspace
./build-container/clutch run --project examples/workspace --target hello::hello
```

Rerun initialization with `bash .devcontainer/post-create.sh`.
`CLUTCH_BUILD_JOBS` adjusts compiler parallelism. See [testing](testing.md) for
suite selection, cleanup and local CI verification.

## Use Docker directly

From the checkout root, build the tools image:

```sh
docker build --file .devcontainer/Dockerfile --tag clutch-dev .devcontainer
```

Start a mounted shell on Linux or macOS:

```sh
docker run --rm -it \
  --mount "type=bind,source=$(pwd),target=/home/clutch-dev/workspace" \
  --workdir /home/clutch-dev/workspace clutch-dev bash
```

In PowerShell:

```powershell
docker run --rm -it --mount "type=bind,source=$($PWD.Path),target=/home/clutch-dev/workspace" --workdir /home/clutch-dev/workspace clutch-dev bash
```

Run `bash .devcontainer/post-create.sh` inside that shell. Plain `docker run`
does not run lifecycle scripts. Exiting removes this temporary container while
preserving files in the mounted checkout. Linux hosts with a UID other than
1000 should use an IDE's devcontainer workflow to apply user-ID mapping.

## Maintain the definition

The `.devcontainer/` build context contains the Dockerfile, package list and
installation/user scripts. BuildKit mounts those inputs during image creation;
project sources stay outside the image. Rebuild after changing image inputs.
`LLVM_VERSION` must remain compatible with `.clang-format`.

The mounted checkout includes Git metadata. Use your normal host authentication,
IDE credential forwarding, or authenticate separately with `gh auth login`
inside the container. Keep credentials outside tracked files and image layers.

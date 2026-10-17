# Shared development environment

Clutch's Dockerfile and devcontainer definition provide the same Ubuntu 26.04
development environment across Linux, macOS and Windows hosts. Shared compiler,
build and test tools make the workflow repeatable across machines. Each developer
creates a container from the checked-in definition and shares source changes
through Git.

The image includes CMake, Ninja, Make, GitHub CLI (`gh`), GCC/G++, Clang 22, clang-format 22,
clang-tidy 22, clangd 22, Graphviz, GDB and Python 3. Its setup script builds Clutch with
CMake and runs CTest. Sources are mounted at `/home/clutch-dev/workspace`, and container
build products stay in `build-container`. The container validates the Linux
workflow; Windows and macOS generators also need their respective native tools.

## Image, container and workspace

The IDE display name is **Clutch**. Its Docker name is
`clutch-development-container`; Docker container names cannot contain spaces.
`devcontainer.json` supplies that name through `runArgs`. The fixed Docker name
allows one such container per Docker engine; use a different name when working
with another checkout simultaneously. Recreate a container to apply changed run
arguments; editing the JSON does not alter an existing container's mounts.

The Docker image contains the development tools. Project files become visible
when the checkout is mounted at `/home/clutch-dev/workspace`. Editing mounted sources
changes the same files on the host. A bare Dockerfile run from an IDE does not
apply `devcontainer.json`, its workspace mount or its initialization command.
An empty `/home/clutch-dev/workspace` therefore indicates a missing mount, not an absent
compiler installation.

In CLion use **Create Dev Container and Mount Sources...** from
`.devcontainer/devcontainer.json`, rather than the Dockerfile's Run action.
Before creating it, check `docker ps -a`: an older container using the fixed name
must be renamed or removed to make that name available. Renaming preserves its
writable layer, for example:

```text
docker rename clutch-development-container clutch-development-container-old
```

Choose an unused backup name. Then create the devcontainer using the IDE steps
below. Sources in the host checkout remain independent of the container layer.

## Verify your workspace

From the attached container's terminal:

```bash
pwd
ls -la /home/clutch-dev/workspace
test -f /home/clutch-dev/workspace/CMakeLists.txt
test -d /home/clutch-dev/workspace/src
CLUTCH_BUILD_JOBS=1 bash .devcontainer/post-create.sh
./build-container/clutch --version
```

The workspace should contain the checkout's sources and setup files. The script
must exit successfully, CTest must report all four checks passing, and the last
command must print the Clutch version. The checks include compiler-backed
Ninja/GNU Make builds and Graphviz rendering when available in the image.
`docker ps` showing a running `sleep infinity` process only confirms that the
container is alive; the build/test results establish that the development
workflow works. For ongoing changes, rerun:

```bash
cmake --build build-container --parallel 1
ctest --test-dir build-container --output-on-failure
```

## Prepare your host

Install Git and clone your fork of Clutch. Set up Docker for your operating system
below, then open the checkout in CLion, VS Code or a terminal. The container
supplies the C++ development tools.

### Linux

Install [Docker Engine for your distribution](https://docs.docker.com/engine/install/)
and start its daemon. Follow Docker's
[Linux post-installation instructions](https://docs.docker.com/engine/install/linux-postinstall/)
to make the daemon accessible from the account that runs your IDE. Run the
readiness commands below from that account; the IDE needs the same Docker access
as its terminal.

### macOS

Install [Docker Desktop for Mac](https://docs.docker.com/desktop/setup/install/mac-install/)
using the installer for Apple silicon or Intel, as appropriate. Start Docker
Desktop and wait for its engine to be ready. Keep it running while using the
container. Allow access to your checkout if Docker Desktop requests file-sharing
permission.

### Windows

Install [Docker Desktop for Windows](https://docs.docker.com/desktop/setup/install/windows-install/)
and select its [WSL 2 backend](https://docs.docker.com/desktop/features/wsl/).
Docker's installation guide covers the required Windows features and hardware
virtualization. Start Docker Desktop and select Linux containers.

Use PowerShell to check WSL:

```powershell
wsl --version
```

If WSL is missing, open an administrator PowerShell and run
`wsl --install --no-distribution`; restart if requested. If WSL is already
installed, run `wsl --update`. Microsoft documents these options in its
[WSL command reference](https://learn.microsoft.com/en-us/windows/wsl/basic-commands).
Install Docker Desktop using the official installer, start it from the Start
menu, and wait for the engine to finish starting. Open a fresh terminal after
installation so Docker's CLI is on `PATH`.

You can open a local Windows checkout in the IDE. If you keep the checkout in a
WSL distribution, enable Docker Desktop integration for that distribution and
open the project from that environment.

### Check Docker access

Run these commands from a host terminal on any supported operating system:

```text
docker version
docker info --format '{{.OSType}}'
```

`docker version` must show both Client and Server sections, and the second command
must print `linux`. A missing Server section means the engine is stopped or the
current account/context cannot reach it. Resolve that connection before opening
the container. See Docker's
[version reference](https://docs.docker.com/reference/cli/docker/version/).

Confirm that the engine can pull and launch a container:

```text
docker run --rm hello-world
```

## Open with CLion

1. Open the checkout in CLion and open `.devcontainer/devcontainer.json`.
2. Use its gutter action **Create Dev Container and Mount Sources...** and select
   the CLion backend.
3. Follow image creation and initialization in the Services tool window.
4. Select **Connect** to open the workspace in JetBrains Client.

This follows [CLion's dev-container workflow](https://www.jetbrains.com/help/clion/start-dev-container-inside-ide.html).
The source mount connects your checkout to the container, so edits appear on
both sides. The first launch downloads the image packages and IDE backend.

Use this toolchain and CMake profile inside the container:

| Setting | Value |
| --- | --- |
| CMake | `/usr/bin/cmake` |
| Build tool | `/usr/bin/ninja` |
| C compiler | `/usr/local/bin/clang` |
| C++ compiler | `/usr/local/bin/clang++` |
| Debugger | `/usr/bin/gdb` |
| CMake generator | Ninja |
| CMake build type | Debug |
| CMake build directory | `build-container` |

These settings reuse the build tree created by initialization.

## Open with VS Code

Install the **Dev Containers** extension, open the checkout and run
**Dev Containers: Reopen in Container** from the command palette. The definition
installs clangd, CMake Tools and the Microsoft C/C++ extension in the container.
Clangd handles completion/diagnostics; the C/C++ extension's IntelliSense and
error squiggles are disabled to avoid duplicate language-service results while
retaining its debugger integration. See
[VS Code's container guide](https://code.visualstudio.com/docs/devcontainers/containers)
for host and extension setup.

Initialization enables CMake's compilation-database export in `build-container`,
and the clangd settings point to that directory. Clutch generates compilation
databases for user projects in those projects' configured profile directories.

### Use Cline in VS Code

Open the project in the devcontainer first. In that attached VS Code window,
open Extensions, find Cline and choose **Install in Dev Container** if offered.
Open Cline from the sidebar and complete its provider setup in the extension UI.
See [Cline's installation guide](https://docs.cline.bot/getting-started/installing-cline)
for the extension setup and
[VS Code's extension-management guide](https://code.visualstudio.com/docs/devcontainers/containers#_managing-extensions)
for container extension placement.

Use the attached window's terminal to confirm that `pwd` points to
`/home/clutch-dev/workspace` and `clang++ --version` resolves the container compiler.
Run build/test commands from that workspace so Cline uses the mounted sources
and Linux toolchain. Extension credentials belong in the extension's settings,
outside the Dockerfile and tracked project files.

## Build and test in the container

The dev-container client runs this initialization script automatically:

```bash
bash .devcontainer/post-create.sh
```

It delegates to `tests/verify.sh`, configures a Debug CMake/Ninja build using
Clang, compiles Clutch and runs CTest. The runner requires the POSIX integration
suite to be registered and exercises Clutch's generated Ninja and GNU Make
builds. You can rerun initialization or use these commands for an existing build:

```bash
cmake --build build-container --parallel 2
ctest --test-dir build-container --output-on-failure
./build-container/clutch --help
```

The script defaults to two compiler jobs. Set `CLUTCH_BUILD_JOBS` to a positive
integer to adjust it:

```bash
CLUTCH_BUILD_JOBS=4 bash .devcontainer/post-create.sh
```

For a lower-memory build, use `CLUTCH_BUILD_JOBS=1`. If Docker reports an
unexpected EOF or the engine restarts during a build, check Docker Desktop's
status and resource usage, then retry with fewer compiler jobs. A connection
failure alone does not establish that the source or Dockerfile is invalid.

The container runs Linux builds and the checks registered by the current CMake
configuration. Validate Visual Studio/MSBuild, NMake and JOM on Windows, and
Xcode on macOS with the corresponding native tools. Use separate build trees for
container and native builds.

To clear generated test workspace folders inside `tests/work`, preview and run:

```bash
bash tests/clean-work.sh --dry-run
bash tests/clean-work.sh
```

Cleanup preserves the `work` directory and loose files. It rejects links inside
target trees. Close processes using test workspaces before cleaning; the next
test run creates fresh workspace folders.

## Inspect a project's build graph

Configuration writes `build-graph.dot` beside the project's native build files.
From a Clutch project in the container, configure and render the selected profile:

```bash
/home/clutch-dev/workspace/build-container/clutch .
dot -Tsvg Clutch-build-debug/build-graph.dot -o Clutch-build-debug/build-graph.svg
dot -Tpng Clutch-build-debug/build-graph.dot -o Clutch-build-debug/build-graph.png
```

Use the profile's `build_dir` path when it differs from the default. Graphviz's
[`dot` command](https://graphviz.org/doc/info/command.html) lays out the graph
and writes the requested image format. Clutch exports DOT using its own code;
Graphviz is a contributor inspection tool rather than a Clutch runtime requirement.

The graph displays source-to-object compilation and object-to-executable linking
for the application and tests. Inspect it for unexpected targets and repeated
compilation. Compiler-discovered header edges and future package dependencies
are not represented. A diagram does not measure build speed or binary size.
Clutch separately rejects dependency cycles and duplicate artifact producers in
its configured build graph before generating or executing a build.

CTest's `clutch.graph` check exercises graph validation and deterministic DOT
serialization. When CMake finds Graphviz, it also renders an escaping fixture to
SVG in the CMake build tree's `graph-tests` directory. The container installs
Graphviz, so this rendering check participates in its normal test run.

## Use Docker from a terminal

With Node.js/npm and Docker available, the Dev Container CLI applies the entire
configuration, including the workspace mount, account and initialization:

```text
npx --yes @devcontainers/cli up --workspace-folder . --remote-env CLUTCH_BUILD_JOBS=1
```

It leaves `clutch-development-container` running. Enter its workspace with:

```text
docker exec -it --user clutch-dev --workdir /home/clutch-dev/workspace clutch-development-container bash
```

Stop it with `docker stop clutch-development-container`; resume with
`docker start clutch-development-container`. These operations preserve the
container's installed tools and the host-mounted sources. IDE creation/attachment
remains available through the CLion and VS Code workflows above.

To build just the reusable image:

Run the image build from the checkout root:

```text
docker build --file .devcontainer/Dockerfile --tag clutch-dev .devcontainer
```

On Linux or macOS, start a mounted workspace with:

```bash
docker run --rm -it \
    --mount "type=bind,source=$(pwd),target=/home/clutch-dev/workspace" \
    --workdir /home/clutch-dev/workspace clutch-dev bash
```

The image runs as `clutch-dev` with UID 1000. On Linux hosts with a different
UID, use the Dev Container CLI or IDE workflow above to apply the configured
UID mapping and keep workspace files writable by the contributor.

On Windows, use PowerShell:

```powershell
docker run --rm -it --mount "type=bind,source=$($PWD.Path),target=/home/clutch-dev/workspace" --workdir /home/clutch-dev/workspace clutch-dev bash
```

Inside the container shell, run `bash .devcontainer/post-create.sh`. A plain
`docker run` does not execute dev-container lifecycle commands automatically.
Exiting removes this container; edits and build products in the mounted checkout
remain available on the host.

## Connect the checkout to GitHub

The source mount includes the checkout's `.git` directory, so host and container
edits belong to the same repository. Authentication is separate from building
the image. You can authenticate and push directly from the container, or use
a host terminal with its own authentication.

### Authenticate inside the container

GitHub CLI is installed in the image. Enter the running workspace from your host:

```text
docker exec -it clutch-development-container bash
```

In that container terminal, check the CLI, authenticate and configure Git's
HTTPS credential helper:

```bash
gh --version
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git --hostname github.com
gh auth status --hostname github.com
```

Complete the displayed device-code sign-in using your host's browser. After
authentication, the commit/push commands below can run in this same terminal.
The container's GitHub sign-in is separate from host authentication and lives
in the container user's home. Reauthenticate after replacing the container;
restarting the same container preserves it.

### Authenticate on the host

Alternatively, with GitHub CLI installed on the host, authenticate through the
browser and configure its Git credential helper:

```text
gh auth login --hostname github.com --git-protocol https --web
gh auth setup-git --hostname github.com
gh auth status
```

These commands follow the official
[login](https://cli.github.com/manual/gh_auth_login) and
[Git helper](https://cli.github.com/manual/gh_auth_setup-git) references.

### Configure the repository and push

Create an empty repository on GitHub without generated files. From the checkout
in your authenticated host or container terminal, set the repository identity
and connect that existing remote, replacing
the example values:

```text
git config user.name "Your Name"
git config user.email "YOUR_GITHUB_EMAIL"
git remote -v
git remote add origin https://github.com/YOUR_ACCOUNT/YOUR_REPOSITORY.git
```

If `origin` already points to the correct repository, keep it. To intentionally
change an existing origin, use `git remote set-url origin <url>` instead of adding
it again. See GitHub's
[remote repository guide](https://docs.github.com/en/get-started/git-basics/managing-remote-repositories).

After reviewing the source changes and running verification, make the initial
commit and push from that authenticated terminal:

```text
git status
git add .
git diff --cached --stat
git commit -m "Initial Clutch build system and shared development environment"
git branch -M main
git push -u origin main
```

Commit the Dockerfile, devcontainer definition, scripts and documentation so other
developers can clone the repository and open the same environment. Generated
build trees and test workspaces are excluded by `.gitignore`.

VS Code Dev Containers can also reuse a host HTTPS credential helper or forward
a running SSH agent for Git commands inside its attached terminal; see
[sharing Git credentials](https://code.visualstudio.com/remote/advancedcontainers/sharing-git-credentials).
A plain `docker run` does not set up that integration. The direct `gh auth login`
workflow above authenticates inside the container without depending on IDE
credential forwarding.

## Maintain the definition

| File | Responsibility |
| --- | --- |
| `.devcontainer/devcontainer.json` | Image build, workspace mount, remote user, editor settings and initialization |
| `.devcontainer/Dockerfile` | Ubuntu development tools, verified LLVM repository and CLion backend libraries |
| `.devcontainer/post-create.sh` | CMake configuration, compilation and CTest execution |
| `tests/verify.sh` | Shared Linux/macOS verification runner used by initialization |
| `.devcontainer/.dockerignore` | Limits the tools-only image context to the Dockerfile and ignore rules |
| `.gitattributes` | Keep shell and setup files in LF format across host platforms |

`devcontainer.json` uses [JSON with Comments](https://containers.dev/overview.html)
to explain the mount, account, initialization and editor settings inline.

The image build context is `.devcontainer/`, so Docker reads `.dockerignore`
directly from that directory. The ignore rules admit only the Dockerfile and
ignore file; sources and initialization scripts remain available through the
workspace bind mount. See Docker's [context and ignore-file rules](https://docs.docker.com/build/concepts/context/#filename-and-location).

The definition uses the `clutch-dev` account and a source bind mount. It does not need
privileged mode or access to the host Docker socket. The
[Dev Container specification](https://containers.dev/implementors/json_reference/)
describes its settings and lifecycle hooks.

The Dockerfile defaults to `ubuntu:26.04`, controlled by `BASE_IMAGE`. It creates
the `clutch-dev` account with a home directory, Bash and passwordless sudo for
interactive development. When the Ubuntu image includes a UID 1000 account,
that account is renamed and its home relocated instead of creating a UID clash.
The image's working directory and the devcontainer mount both use
`/home/clutch-dev/workspace`.

Initialization registers only this checkout as a Git `safe.directory` in the
container user's configuration, allowing Windows bind mounts with different
reported owners to work. This step is limited to Docker execution and never
adds a wildcard trust rule or changes host Git configuration.

LLVM packages come from the official [apt.llvm.org repository](https://apt.llvm.org/).
The Dockerfile verifies its published signing-key fingerprint and scopes the key
to that repository. The Ubuntu codename is read from `/etc/os-release`, so the
26.04 image uses the Resolute repository; alternate Ubuntu bases use their own
matching repository when LLVM 22 packages are available there. The LLVM major
version is fixed at 22; the base image digest
and individual package revisions are not pinned, so rebuilding can incorporate
upstream updates. Review toolchain changes and rebuild the image when maintaining
the definition. CLion and VS Code provide actions to rebuild their container.

The first build needs access to the base-image registry and Ubuntu/LLVM package
servers. IDE backend and extension downloads are managed by the corresponding IDE.

## Validation status

Validated on 2026-10-18 with Docker Desktop on Windows using Ubuntu 26.04 on
`linux/amd64`. The Dockerfile built successfully, and the official Dev Container
CLI created the persistent container with its writable project mount.
Initialization ran as `clutch-dev` with Clang 22.1.8 and `CLUTCH_BUILD_JOBS=1`.
All four CTest checks
passed: help, version, graph validation/rendering, and the POSIX Ninja/GNU Make
integration suite. The complete manifest-reference example and entry-only C/C++
examples also configured and built; the executable test example passed.

The user/home directory, source writes, passwordless sudo, Git access, compiler
and debugger tools, Clutch startup and build-local IDE database were checked.
Rerunning initialization from `/tmp` also passed. IDE attachment remains to be
checked in CLion and VS Code.
Native Windows and macOS generators require their respective host validation
in addition to these Linux checks; the container does not run Xcode or MSBuild.

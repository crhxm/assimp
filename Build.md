# Build / Install Instructions

## Manual build instructions
### Install prerequisites
You need to install
* cmake
* Your compiler (must support C++17 and C99 at least)
* For Windows
  *  DX-SDK 9 if you want to use our 3D-Viewer

### Get the source
Make sure you have a working git-installation. Open a command prompt and clone the Asset-Importer-Lib via:
```bash
git clone https://github.com/assimp/assimp.git
```
### Build from source:
* For *assimp.lib* without any tools:
```bash
cd assimp
cmake CMakeLists.txt
cmake --build .
```

* For assimp with the common tools like *assimp-cmd*
```bash
cd assimp
cmake CMakeLists.txt -DASSIMP_BUILD_ASSIMP_TOOLS=ON
cmake --build .
```
Note that by default this builds a shared library into the `bin` directory. If you want to build it as a static library see the build options at the bottom of this file.

### Build instructions for Windows with Visual-Studio
First, you have to install Visual-Studio on your windows-system. You can get the Community-Version for free here: https://visualstudio.microsoft.com/de/downloads/
To generate the build environment for your IDE open a command prompt, navigate to your repo and type:
```bash
cmake CMakeLists.txt
```
This will generate the project files for the visual studio. All dependencies used to build Asset-Importer-Lib shall be part of the repo. If you want to use you own zlib installation this is possible as well. Check the options for it.

### Build instructions for Windows with UWP
See <https://stackoverflow.com/questions/40803170/cmake-uwp-using-cmake-to-build-universal-windows-app>

### Build instructions for MinGW
 Older versions of MinGW's compiler (e.g. 5.1.0) do not support the -mbig_obj flag
required to compile some of assimp's files, especially for debug builds.
Version 7.3.0 of g++-mingw-w64 & gcc-mingw-w64 appears to work.

Please see [CMake Cross Compiling](https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html#cross-compiling) for general information on CMake Toolchains.

Some users have had success building assimp using MinGW on Linux using [polly](https://github.com/ruslo/polly/).

The following toolchain, which is not maintained by assimp, seems to work on Linux: [linux-mingw-w64-gnuxx11.cmake](https://github.com/ruslo/polly/blob/master/linux-mingw-w64-gnuxx11.cmake)

The following toolchain may or may not be helpful for building assimp using MinGW on Windows (untested):
 [mingw-cxx17.cmake](https://github.com/ruslo/polly/blob/master/mingw-cxx17.cmake)

Besides the toolchain, compilation should be the same as for Linux / Unix.

### CMake build options
The cmake-build-environment provides options to configure the build. The following options can be used:
- **ASSIMP_HUNTER_ENABLED (default OFF)**: Enable Hunter package manager support.
- **BUILD_SHARED_LIBS (default ON)**: Generation of shared libs (dll for windows, so for Linux). Set this to OFF to get a static lib.
- **ASSIMP_BUILD_FRAMEWORK (default OFF, MacOnly)**: Build package as Mac OS X Framework bundle.
- **ASSIMP_DOUBLE_PRECISION (default OFF)**: All data will be stored as double values.
- **ASSIMP_OPT_BUILD_PACKAGES (default OFF)**: Set to ON to generate CPack configuration files and packaging targets.
- **ASSIMP_ANDROID_JNIIOSYSTEM (default OFF)**: Android JNI IOSystem support is active.
- **ASSIMP_NO_EXPORT (default OFF)**: Disable Assimp's export functionality.
- **ASSIMP_BUILD_ZLIB (default OFF)**: Build our own zlib.
- **ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT (default ON)**: Build Assimp with all exporters enabled.
- **ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT (default ON)**: Build Assimp with (most) importers enabled. Currently, USD importer is not included. See ASSIMP_BUILD_USD_IMPORTER.
- **ASSIMP_BUILD_ASSIMP_TOOLS (default OFF)**: If the supplementary tools for Assimp are built in addition to the library.
- **ASSIMP_BUILD_SAMPLES (default OFF)**: If the official samples are built as well (needs Glut).
- **ASSIMP_BUILD_TESTS (default ON)**: If the test suite for Assimp is built in addition to the library.
- **ASSIMP_COVERALLS (default OFF)**: Enable this to measure test coverage.
- **ASSIMP_INSTALL (default ON)**: Install Assimp library. Disable this if you want to use Assimp as a submodule.
- **ASSIMP_WARNINGS_AS_ERRORS (default ON)**: Treat all warnings as errors.
- **ASSIMP_ASAN (default OFF)**: Enable AddressSanitizer.
- **ASSIMP_UBSAN (default OFF)**: Enable Undefined Behavior sanitizer.
- **ASSIMP_BUILD_DOCS (default OFF)**: Build documentation using Doxygen. OBSOLETE, see https://github.com/assimp/assimp-docs
- **ASSIMP_INJECT_DEBUG_POSTFIX (default ON)**: Inject debug postfix in .a/.so/.lib/.dll lib names
- **ASSIMP_IGNORE_GIT_HASH (default OFF)**: Don't call git to get the hash.
- **ASSIMP_INSTALL_PDB (default ON)**: Install MSVC debug files.
- **USE_STATIC_CRT (default OFF)**: Link against the static MSVC runtime libraries.
- **ASSIMP_BUILD_DRACO (default OFF)**: Build Draco libraries. Primarily for glTF.
- **ASSIMP_BUILD_ASSIMP_VIEW (default ON, if DirectX found, OFF otherwise)**: Build Assimp view tool (requires DirectX).
- **ASSIMP_BUILD_USD_IMPORTER (default OFF, requires ASSIMP_WARNINGS_AS_ERRORS to be OFF)**: Build USD importer, defaults to off for CI purposes

### Install prebuild binaries
## Install on all platforms using vcpkg
You can download and install assimp using the [vcpkg](https://github.com/Microsoft/vcpkg/) dependency manager:
```bash
    git clone https://github.com/Microsoft/vcpkg.git
    cd vcpkg
    ./bootstrap-vcpkg.sh
    ./vcpkg integrate install
    ./vcpkg install assimp
```
The assimp port in vcpkg is kept up to date by Microsoft team members and community contributors. If the version is out of date, please [create an issue or pull request](https://github.com/Microsoft/vcpkg) on the vcpkg repository.

### Install on Ubuntu
You can install the Asset-Importer-Lib via apt:
```
sudo apt-get update
sudo apt-get install libassimp-dev
```

### Install pyassimp
You need to have pip installed:
```
pip install pyassimp
```

### Get the SDK from itchi.io
Just check [itchi.io](https://kimkulling.itch.io/the-asset-importer-lib)

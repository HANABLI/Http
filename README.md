# Http
This is a library which implements common types for the RFC 7230, "Hypertext Transfer Protocol (HTTP/1.1): Message Syntax and Routing".

The purpose of this library is to provide `Request` and `Response` types which
can be used parse and generate Hypertext Transfer Protocol (HTTP) requests and
responses.

## Building the C++ Implementation

A portable library is built which depends on the C++11 compiler, the C++
standard library, and non-standard dependencies listed below.  It should be
supported on almost any platform.  The following are recommended toolchains for
popular platforms.

* Windows -- [Visual Studio](https://www.visualstudio.com/) (Microsoft Visual
  C++)
* Linux -- clang or gcc
* MacOS -- Xcode (clang)

This library is not intended to stand alone.  It is intended to be included in
a larger solution which uses [CMake](https://cmake.org/) to generate the build
system and build applications which will link with the library.

There are two distinct steps in the build process:

1. Generation of the build system, using CMake
2. Compiling, linking, etc., using CMake-compatible toolchain

### Prerequisites

* [CMake](https://cmake.org/) version 3.8 or newer
* C++11 toolchain compatible with CMake for your development platform (e.g.
  [Visual Studio](https://www.visualstudio.com/) on Windows)
* [MessageHeaders](https://github.com/HANABLI/MessageHeaders.git) - a library
  which can parse and generate e-mail or web message headers
* [StringUtils](https://github.com/HANABLI/StringExtensions.git) - a
  library containing C++ string-oriented libraries, many of which ought to be
  in the standard library, but aren't.
* [SystemUtils](https://github.com/HANABLI/SystemAbstractions.git) - a
  cross-platform adapter library for system services whose APIs vary from one
  operating system to another
* [Uri](https://github.com/HANABLI/Uri.git) - a library that can parse and
  generate Uniform Resource Identifiers (URIs)
* [zlib](https://github.com/madler/zlib.git) - Foundational compression library

### Build system generation

Generate the build system using [CMake](https://cmake.org/) from the solution
root.  For example:

```bash
mkdir build
cd build
cmake -G "Visual Studio 15 2017" -A "x64" ..
```

### Compiling, linking, et cetera

Either use [CMake](https://cmake.org/) or your toolchain's IDE to build.
For [CMake](https://cmake.org/):

```bash
cd build
cmake --build . --config Release
```
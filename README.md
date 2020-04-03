# Arcana Samples

This repository contains samples and tests for all arcana libraries.


## Structure

* `samples/` contains samples, some library-specific, some general
* `tests/` contains unit and integration tests for all arcana libraries with a few exceptions
    * `typed-geometry` has a separate `tg-samples` repo
    * `polymesh` has a separate `polymesh-samples` repo


## Getting Started (Windows 10)

* Make sure Windows SDK is installed (https://developer.microsoft.com/en-us/windows/downloads/windows-10-sdk/)
* If Vulkan backend should be used, Vulkan SDK must be installed (https://www.lunarg.com/vulkan-sdk/)
* Execute root `CMakeLists.txt`
* Open `Arcana.sln` with Visual Studio (2017, 2019)
* Compile

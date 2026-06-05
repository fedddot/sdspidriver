# sdspidriver

## Purpose

**sdspidriver** is a simple, platform-independent C++17 driver that provides SD card interfacing functionality over SPI. It supports SD v1, SD v2, SDHC, and SDXC cards and exposes a minimal API for reading and writing 512-byte blocks.

The project is inspired by [ulibSD](https://github.com/1nv1/ulibSD), but simplifies the initialisation flow and fixes several issues present in the original — most notably, correct support for SDHC and SDXC cards, which the original project does not handle properly.

The driver is platform-agnostic: SPI communication is provided entirely through callbacks (init, set speed, transceive byte, chip select), making it straightforward to integrate on any platform or RTOS without depending on a specific HAL.

## Integration

### With CMake FetchContent

```cmake
include(FetchContent)

FetchContent_Declare(
    sdspidriver
    GIT_REPOSITORY https://github.com/fedddot/sdspidriver.git
    GIT_TAG        main   # pin to a specific tag or commit for reproducible builds
)

FetchContent_MakeAvailable(sdspidriver)
```

Then link the library:

```cmake
target_link_libraries(my_target PRIVATE sdspidriver)
```

### With add_subdirectory

Clone or copy the repository into your project tree, then:

```cmake
add_subdirectory(sdspidriver)

target_link_libraries(my_target PRIVATE sdspidriver)
```

FetchContent_Declare(
    fatfs
    GIT_REPOSITORY  https://github.com/abbrev/fatfs.git
    GIT_TAG         R0.16
)
FetchContent_MakeAvailable(fatfs)

add_library(ff INTERFACE)
target_sources(ff INTERFACE ${fatfs_SOURCE_DIR}/source/ff.c ${CMAKE_CURRENT_LIST_DIR}/src/fatfs_callbacks.cpp)
target_include_directories(ff INTERFACE ${fatfs_SOURCE_DIR}/source)
target_link_libraries(ff INTERFACE sd_spi_driver)
target_compile_definitions(ff INTERFACE FFCONF_DEF=80386)
target_compile_options(
    ff
    INTERFACE
    -include ${CMAKE_CURRENT_LIST_DIR}/include/custom_ffconf.h
)
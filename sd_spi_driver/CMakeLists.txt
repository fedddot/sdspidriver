add_library(sd_spi_driver INTERFACE)
target_sources(sd_spi_driver INTERFACE ${CMAKE_CURRENT_LIST_DIR}/src/sd_spi_driver.cpp)
target_include_directories(sd_spi_driver INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
add_library(sd_spi_driver STATIC ${CMAKE_CURRENT_LIST_DIR}/src/sd_spi_driver.cpp)
target_include_directories(sd_spi_driver PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
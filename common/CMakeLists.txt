# common/CMakeLists.txt

# static library for common functionalities
add_library(common_lib STATIC
    src/NetconfSession.cpp
    # some other common source files
)

# Header files directory
target_include_directories(common_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Activating Netconf SSH support
target_compile_definitions(common_lib PRIVATE NC_ENABLED_SSH)

target_include_directories(common_lib PRIVATE ${LIBNETCONF2_INCLUDE_DIR})
target_link_libraries(common_lib PRIVATE ${LIBNETCONF2_LIBRARY})
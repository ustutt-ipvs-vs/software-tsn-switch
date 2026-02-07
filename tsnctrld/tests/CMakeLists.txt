# TSNCTRLD TESTS BUILD CONFIGURATION
# ==================================

include(GoogleTest)

# Helper function for all test executables
function(add_gtest TARGET)
    add_executable(${TARGET} ${ARGN})

    target_link_libraries(${TARGET}
        PRIVATE
        GTest::gtest
        GTest::gtest_main
        tsnctrld_lib
        PkgConfig::SPDLOG
    )

    target_compile_features(${TARGET} PRIVATE cxx_std_20)

    gtest_discover_tests(${TARGET})
endfunction()


# List of all test executables
# ----------------------------
add_gtest(nested_attr_builder_test NestedAttrBuilderTest.cpp)
# To add a new test file:
# add_gtest(<new_executable_name> <test_file>)

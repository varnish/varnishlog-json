cmake_minimum_required(VERSION 3.16.0)
project(varnishlog-json C)

# just use pkg-config to find the varnish libs
find_package(PkgConfig)
pkg_check_modules(VARNISH REQUIRED IMPORTED_TARGET varnishapi>6.0)
pkg_check_modules(CJSON REQUIRED IMPORTED_TARGET libcjson)

add_compile_options(-Wall -Wextra -Wpedantic)

# what's in our executable
add_executable(varnishlog-json main.c varnishlog-json_options.h)
# link against the varnish libraries
target_link_libraries(varnishlog-json PkgConfig::VARNISH cjson)
# need this to find varnish-varnishlog-json_options.h
target_include_directories(varnishlog-json PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
install(TARGETS varnishlog-json)

# man page
find_program(RST2MAN rst2man)
add_custom_command(OUTPUT varnishlog-json_options.rst
		   COMMAND "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json" --options > "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json_options.rst"
		   DEPENDS varnishlog-json)
add_custom_command(OUTPUT varnishlog-json_synopsis.rst
		   COMMAND "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json" --synopsis > "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json_synopsis.rst"
		   DEPENDS varnishlog-json)
# varnishlog-json.rst includes file from the build directory, so it needs to be copied over BUT
# only if source != build, otherwise we'd copy it over itself
if(NOT "${CMAKE_CURRENT_SOURCE_DIR}" STREQUAL "${CMAKE_CURRENT_BINARY_DIR}")
	message(STATUS "-----------------------NOT EQUAL ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}")
	add_custom_command(OUTPUT varnishlog-json.rst
			   COMMAND cp "${CMAKE_CURRENT_SOURCE_DIR}/varnishlog-json.rst" "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json.rst"
			   DEPENDS varnishlog-json varnishlog-json.rst)
endif()
add_custom_command(OUTPUT varnishlog-json.1
		   COMMAND ${RST2MAN} "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json.rst" "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json.1"
		   DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json.rst" varnishlog-json_options.rst varnishlog-json_synopsis.rst)
add_custom_target(man ALL DEPENDS varnishlog-json.1)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json.1" TYPE MAN)

# tests
enable_testing()
find_program(VTEST varnishtest)
file(GLOB vtcs "tests/*.vtc")
foreach(vtc ${vtcs})
	add_test(NAME ${vtc}
		COMMAND ${VTEST} -D varnishlog-json_bin="${CMAKE_CURRENT_BINARY_DIR}/varnishlog-json" ${vtc})
endforeach()

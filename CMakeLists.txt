cmake_minimum_required(VERSION 3.13)

cmake_policy(SET CMP0048 NEW)
project(another-toml VERSION 0.9 LANGUAGES CXX)

add_subdirectory(uni-algo)

option(ANOTHER_TOML_USE_CHRONO "Enable cpp chrono type in toml files")

add_library(another-toml-cpp)
set_property(TARGET another-toml-cpp PROPERTY CXX_STANDARD 17)

if(BUILD_SHARED_LIBS)
    set_target_properties(another-toml-cpp PROPERTIES DEBUG_POSTFIX -d)
    set_target_properties(another-toml-cpp PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else() 
    set_target_properties(another-toml-cpp PROPERTIES DEBUG_POSTFIX -s-d)
    set_target_properties(another-toml-cpp PROPERTIES RELEASE_POSTFIX -s)
endif()

if(ANOTHER_TOML_USE_CHRONO)
	target_compile_definitions(another-toml-cpp PUBLIC ANOTHER_TOML_USE_CHRONO)
endif()

cmake_policy(SET CMP0076 NEW)

target_sources(another-toml-cpp
	PRIVATE
	./source/another_toml.cpp
	./source/durations.cpp
	./source/string_util.cpp
	PUBLIC FILE_SET HEADERS
	FILES
	./include/another_toml/another_toml.hpp
	./include/another_toml/durations.hpp
	./include/another_toml/except.hpp
	./include/another_toml/internal.hpp
	./include/another_toml/node.hpp
	./include/another_toml/node.inl
	./include/another_toml/parser.hpp
	./include/another_toml/string_util.hpp
	./include/another_toml/types.hpp
	./include/another_toml/writer.hpp
	./include/another_toml/writer.inl
)

target_include_directories(another-toml-cpp PUBLIC ./include)
target_link_libraries(another-toml-cpp PRIVATE uni-algo)

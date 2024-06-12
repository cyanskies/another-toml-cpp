cmake_minimum_required(VERSION 3.13)

add_subdirectory(uni-algo)

cmake_policy(SET CMP0048 NEW)
project(another-toml-cpp VERSION 0.9 LANGUAGES CXX)

add_library(${PROJECT_NAME})
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)

if(BUILD_SHARED_LIBS)
    set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX -d)
    set_target_properties(${PROJECT_NAME} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else() 
    set_target_properties(${PROJECT_NAME} PROPERTIES DEBUG_POSTFIX -s-d)
    set_target_properties(${PROJECT_NAME} PROPERTIES RELEASE_POSTFIX -s)
endif()

cmake_policy(SET CMP0076 NEW)

target_sources(${PROJECT_NAME}
	PUBLIC
	./include/another_toml/another_toml.hpp
	./include/another_toml/except.hpp
	./include/another_toml/internal.hpp
	./include/another_toml/node.hpp
	./include/another_toml/node.inl
	./include/another_toml/parser.hpp
	./include/another_toml/string_util.hpp
	./include/another_toml/types.hpp
	./include/another_toml/writer.hpp
	./include/another_toml/writer.inl
	PRIVATE
	./source/another_toml.cpp
	./source/string_util.cpp
)

target_include_directories(${PROJECT_NAME} PUBLIC ./include)
target_link_libraries(${PROJECT_NAME} PRIVATE uni-algo)

// MIT License
//
// Copyright(c) 2022 Steven Pilkington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifndef ANOTHER_TOML_PARSER_HPP
#define ANOTHER_TOML_PARSER_HPP

#include <filesystem>

#include "another_toml/node.hpp"

namespace another_toml
{
	// Parse a TOML document.
	root_node parse(std::string_view toml);
	root_node parse(const std::string& toml);
	root_node parse(const char* toml);
	root_node parse(std::istream&);
	// NOTE: user must handle std exceptions related to file reading
	// eg. std::filesystem_error and its children.
	root_node parse(const std::filesystem::path& filename);

	// Parse a TOML document without throwing another_toml exceptions
	// Errors are reported to std::cerr.
	root_node parse(std::string_view toml, no_throw_t);
	root_node parse(const std::string& toml, no_throw_t);
	root_node parse(const char* toml, no_throw_t);
	root_node parse(std::istream&, no_throw_t);
	root_node parse(const std::filesystem::path& filename, no_throw_t);
}

#endif

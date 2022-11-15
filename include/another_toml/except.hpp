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

#ifndef ANOTHER_TOML_EXCEPT_HPP
#define ANOTHER_TOML_EXCEPT_HPP

#include <stdexcept>

namespace another_toml
{
	//thown by any of the parse functions
	class toml_error : public std::runtime_error
	{
	public:
		using std::runtime_error::runtime_error;
	};

	//thrown if eof is encountered in an unexpected location(inside a quote or table name, etc.)
	class unexpected_eof : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown when encountering an unexpected character
	class unexpected_character :public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown if the toml file contains duplicate table or key declarations
	class duplicate_element : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// Thrown by basic_node when calling fucntions on a node where good() == false
	class bad_node : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	//thrown when calling node::as_int... if the type
	// stored doesn't match the function return type
	class wrong_type : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown if calling a function that isn't 
	// supported by the current node type
	class wrong_node_type : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown by some functions that search for keys,
	// but do not have another way to report failure
	class node_not_found : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown from unicode handling functions(in string_util.hpp)
	// also thrown while parsing or writing unicode text
	class unicode_error : public toml_error
	{
	public:
		using toml_error::toml_error;
	};

	// thrown if an invalid raw unicode or escaped unicode char was found
	class invalid_unicode_char : public unicode_error
	{
	public:
		using unicode_error::unicode_error;
	};
}

#endif

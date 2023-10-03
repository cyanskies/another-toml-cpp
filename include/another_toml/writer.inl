// MIT License
//
// Copyright (c) 2022 Steven Pilkington
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "another_toml/writer.hpp"

namespace another_toml
{
	template<typename Integral,
		std::enable_if_t<detail::is_integral_v<Integral>, int>>
		void writer::write_value(Integral i, int_base base)
	{
		write_value(std::int64_t{ i }, base);
		return;
	}

	template<typename T>
	void writer::write(std::string_view key, T&& value)
	{
		using Ty = std::decay_t<T>;
		if constexpr (detail::is_range_v<Ty> && !std::is_convertible_v<T, std::string_view>)
		{
			begin_array(key);
			for (auto&& v : std::forward<T>(value))
				write_value(std::move(v));
			end_array();
		}
		else
		{
			write_key(key);
			write_value(std::forward<T>(value));
		}
		return;
	}

	template<typename String,
		std::enable_if_t<std::is_convertible_v<String, std::string_view>, int>>
		void writer::write(std::string_view key, String&& value, literal_string_t)
	{
		write_key(key);
		write_value(std::forward<String>(value), literal_string_tag);
		return;
	}

	template<typename Container,
		std::enable_if_t<detail::is_range_v<Container>&&
		std::is_convertible_v<typename Container::value_type, std::string_view>, int>>
		void writer::write(std::string_view key, Container&& value, literal_string_t)
	{
		begin_array(key);
		for (auto&& v : std::forward<Container>(value))
			write_value(std::move(v), literal_string_tag);
		end_array();
		return;
	}

	template<typename Integral,
		std::enable_if_t<detail::is_integral_v<Integral>, int>>
		void writer::write(std::string_view key, Integral value, int_base base)
	{
		write_key(key);
		write_value(value, base);
		return;
	}

	template<typename Container,
		std::enable_if_t<detail::is_range_v<Container>&&
		detail::is_integral_v<typename Container::value_type>, int>>
		void writer::write(std::string_view key, Container value, int_base base)
	{
		begin_array(key);
		for (auto&& v : std::forward<Container>(value))
			write_value(std::move(v), base);
		end_array();
		return;
	}

	template<typename Container,
		std::enable_if_t<detail::is_range_v<Container>&&
		std::is_convertible_v<typename Container::value_type, double>, int>>
		void writer::write(std::string_view key, Container value,
			float_rep rep, std::int8_t prec)
	{
		begin_array(key);
		for (auto&& v : std::forward<Container>(value))
			write_value(std::move(v), rep, prec);
		end_array();
		return;
	}

	template<typename T>
	void writer::write(std::string_view key, std::initializer_list<T> value)
	{
		begin_array(key);
		for (auto&& v : value)
			write_value(std::move(v));
		end_array();
		return;
	}

	template<typename String,
		std::enable_if_t<std::is_convertible_v<String, std::string_view>, int>>
		void writer::write(std::string_view key, std::initializer_list<String> value, literal_string_t)
	{
		begin_array(key);
		for (auto&& v : value)
			write_value(std::move(v), literal_string_tag);
		end_array();
		return;
	}

	template<typename Integral,
		std::enable_if_t<detail::is_integral_v<Integral>, int>>
		void writer::write(std::string_view key,
			std::initializer_list<Integral> value, int_base base)
	{
		begin_array(key);
		for (auto&& v : value)
			write_value(std::move(v), base);
		end_array();
		return;
	}

	template<typename Floating,
		std::enable_if_t<std::is_convertible_v<Floating, double>, int>>
		void writer::write(std::string_view key, std::initializer_list<Floating> value,
			float_rep rep, std::int8_t prec)
	{
		begin_array(key);
		for (auto&& v : value)
			write_value(std::move(v), rep, prec);
		end_array();
		return;
	}
}

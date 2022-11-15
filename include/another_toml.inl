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
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "another_toml.hpp"

namespace another_toml
{
	template<bool R>
	template<typename T>
	T basic_node<R>::get_value(std::string_view key_name) const
	{
		const auto key = find_child(key_name);
		if (!key.good())
			throw node_not_found{ "Unable to find key" };
		
		return key.as_t<T>();
	}

	template<bool R>
	template<typename T>
	T basic_node<R>::get_value(std::string_view key_name, T def) const
	{
		try
		{
			return get_value<T>(key_name);
		}
		catch (const node_not_found&)
		{
			return def;
		}
	}

	template<bool R>
	template<typename T>
	T basic_node<R>::as_type() const
	{
		if constexpr (detail::is_container_v<T>)
		{
			static_assert(detail::is_toml_type<typename T::value_type>,
				"Extracting arrays requires the target type to be an exact TOML type");

			if (!array())
				throw wrong_node_type{ "Error: calling as_t with a container type requires this to be an array node" };

			auto out = T{};
			for (const auto n : *this)
				out.push_back(n.as_type<typename T::value_type>());

			return out;
		}
		else
		{
			static_assert(detail::is_toml_type<T>, "as_t() must be called with an exact TOML type.")

			if constexpr (std::is_same_v<T, std::int64_t>)
				return as_integer();
			else if constexpr (std::is_same_v<T, double>)
				return as_floating();
			else if constexpr (std::is_same_v<T, bool>)
				return as_boolean();
			else if constexpr (std::is_same_v<T, std::string>)
				return as_string();
			else if constexpr (std::is_same_v<T, date>)
				return as_date_local();
			else if constexpr (std::is_same_v<T, time>)
				return as_time_local();
			else if constexpr (std::is_same_v<T, local_date_time>)
				return as_date_time_local();
			else if constexpr (std::is_same_v<T, date_time>)
				return as_date_time();
		}
		else
			throw wrong_type{ "Unable to convert node to type" };
	}

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
		if constexpr (detail::is_range_v<Ty>)
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
		std::enable_if_t<detail::is_range_v<Container> &&
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
		std::enable_if_t<detail::is_range_v<Container> &&
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
		std::enable_if_t<detail::is_range_v<Container> &&
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

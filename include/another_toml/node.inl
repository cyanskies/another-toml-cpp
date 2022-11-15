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

#include "another_toml/node.hpp"

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
}

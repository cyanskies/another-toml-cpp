#include "another_toml.hpp"

namespace another_toml
{
	template<bool R>
	template<typename T>
	T basic_node<R>::get_value(std::string_view key_name) const
	{
		const auto key = find_child(key_name);
		if (!key.good())
			throw key_not_found{ "Unable to find key" };
		
		const auto val_node = key.get_first_child();
		return val_node.as_t<T>();
	}

	template<bool R>
	template<typename T>
	T basic_node<R>::get_value(std::string_view key_name, T def) const
	{
		try
		{
			return get_value<T>(key_name);
		}
		catch (const parser_error&)
		{
			return def;
		}
	}

	template<bool R>
	template<typename T>
	T basic_node<R>::as_t() const
	{
		if constexpr (detail::is_container_v<T>)
		{
			if (!array())
				throw wrong_node_type{ "Error: calling as_t with a container type requires this to be an array node" };

			auto out = T{};
			for (const auto n : *this)
				out.push_back(n.as_t<T::value_type>());

			return out;
		}
		else if constexpr (std::is_same_v<T, std::int64_t>)
			return as_int();
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
		else
			throw wrong_type{ "Unable to convert node to type" };
	}
}

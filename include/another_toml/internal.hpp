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

#ifndef ANOTHER_TOML_INTERNAL_HPP
#define ANOTHER_TOML_INTERNAL_HPP

#include <cstddef>
#include <numeric>
#include <type_traits>

#include "another_toml/types.hpp"

namespace another_toml
{
	namespace detail
	{
		// Internal value, used to indicate that the index of a node
		// couldn't be found (or creation of a node failed).
		using index_t = std::size_t;
		constexpr auto bad_index = std::numeric_limits<detail::index_t>::max();

		// Trait for iterable ranges (excluding string).
		template<typename Range, typename = void>
		constexpr auto is_range_v = false;
		template<typename Range>
		constexpr auto is_range_v<Range,
			std::void_t<
			decltype(std::declval<Range>().begin()),
			decltype(std::declval<Range>().end())
			>
		> = !std::is_same_v<Range, std::string>;

		// Trait for appendable containers (excluding string).
		template<typename Cont, typename = void>
		constexpr auto is_container_v = false;
		template<typename Cont>
		constexpr auto is_container_v<Cont,
			std::void_t<
			typename Cont::value_type,
			decltype(std::declval<Cont>().push_back(typename Cont::value_type{}))
			>
		> = !std::is_same_v<Cont, std::string>;

		// Trait for integral types, exluding bool.
		template<typename Integral>
		constexpr auto is_integral_v = std::is_integral_v<Integral> &&
			!std::is_same_v<Integral, bool>;

		// Internal data storage type
		struct toml_internal_data;
		// Deleter above type
		struct toml_data_deleter
		{
		public:
			void operator()(toml_internal_data*) noexcept;
		};

		// Returns the sibling node of index_t, or bad_index.
		index_t get_next(const toml_internal_data&, index_t) noexcept;

		template<typename T>
		constexpr auto is_toml_type = std::is_same_v<T, std::int64_t> ||
			std::is_same_v<T, double> ||
			std::is_same_v<T, bool> ||
			std::is_same_v<T, std::string> ||
			std::is_same_v<T, date> ||
			std::is_same_v<T, time> ||
			std::is_same_v<T, local_date_time> ||
			std::is_same_v<T, date_time>;
	}
}

#endif

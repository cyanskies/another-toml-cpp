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

#include "another_toml/durations.hpp"

#include <array>
#include <charconv>
#include <string>

#include "another_toml/except.hpp"

#ifdef ANOTHER_TOML_USE_CHRONO
namespace another_toml::util
{
	constexpr auto nanos_in_hours = std::nano::den * std::chrono::hours::period::num;
	constexpr auto nanos_in_mins = std::nano::den * std::chrono::minutes::period::num;
	constexpr auto nanos_in_second = std::nano::den;
	constexpr auto nanos_in_milli = std::nano::den / std::milli::den;
	constexpr auto nanos_in_micro = std::nano::den / std::micro::den;

	template<typename Integral>
	std::string to_string(Integral i)
	{
		auto buffer = std::array<char, 21>{};
		auto res = std::to_chars(data(buffer), data(buffer) + size(buffer), i);
		if (res.ec != std::errc())
			throw toml_error{ std::make_error_code(res.ec).message() };
		
		return std::string{ data(buffer), res.ptr };
	}

	std::string duration_to_string(duration d)
	{
		using namespace std::string_literals;
		const auto count = d.count();

		using namespace std::chrono;

		if (count % nanos_in_hours == 0)
		{
			const auto hours_duration = duration_cast<hours>(d);
			const auto hours_count = hours_duration.count();
			return to_string(hours_count) + "h";
		}

		if (count % nanos_in_mins == 0)
		{
			const auto mins_duration = duration_cast<minutes>(d);
			const auto mins_count = mins_duration.count();
			return to_string(mins_count) + "min";
		}

		if (count % nanos_in_second == 0)
		{
			const auto seconds_duration = duration_cast<seconds>(d);
			const auto seconds_count = seconds_duration.count();
			return to_string(seconds_count) + "s";
		}

		if (count % nanos_in_milli == 0)
		{
			const auto milli_duration = duration_cast<milliseconds>(d);
			const auto milli_count = milli_duration.count();
			return to_string(milli_count) + "ms";
		}

		if (count % nanos_in_micro == 0)
		{
			const auto micro_duration = duration_cast<microseconds>(d);
			const auto micro_count = micro_duration.count();
			return to_string(micro_count) + "us";
		}

		return to_string(count) + "ns";
	}
}

#endif

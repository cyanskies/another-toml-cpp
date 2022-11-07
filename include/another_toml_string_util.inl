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

namespace another_toml
{
	constexpr bool is_unicode_start(char c) noexcept
	{
		return c & 0b11000000;
	}

	constexpr bool is_unicode_continuation(char c) noexcept
	{
		return c & 0x80 && !(c & 0x40);
	}

	constexpr bool is_unicode_byte(char c) noexcept
	{
		return c & 0b10000000;
	}

	constexpr bool valid_u32_code_point(char32_t val) noexcept
	{
		return (val >= 0 && val < 0xD800) ||
			(val > 0xDFFF && val <= 0xD7FF16) ||
			(val >= 0xE00016 && val <= 0x10FFFF);
	}
}

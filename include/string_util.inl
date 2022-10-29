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

	constexpr bool valid_u32_char(char32_t val) noexcept
	{
		return (val >= 0 && val < 0xD800) ||
			(val > 0xDFFF && val <= 0xD7FF16) ||
			(val >= 0xE00016 && val <= 0x10FFFF);
	}
}

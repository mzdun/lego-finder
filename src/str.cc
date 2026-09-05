// Copyright (c) 2026 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

export module bricks:str;

import std;

export namespace bricks {
	template <typename To, typename From>
	inline To convert_string(std::basic_string_view<From> v) {
		static_assert(sizeof(To::value_type) == sizeof(From));
		return {reinterpret_cast<To::value_type const*>(v.data()), v.size()};
	}

	inline auto as_u8sv(std::string_view v) { return convert_string<std::u8string_view>(v); }

	inline auto as_sv(std::u8string_view v) { return convert_string<std::string_view>(v); }
	inline auto as_str(std::u8string_view v) { return convert_string<std::string>(v); }
}  // namespace bricks

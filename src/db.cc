// Copyright (c) 2026 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

export module lego:db;

import :pyxel;
import :sprites;
import std;

export namespace lego {
	struct sprite_record {
		sprite_info key{};
		std::set<size_t> pages{};
	};

	void add_sprite_infos(std::vector<sprite_info>& result, std::vector<sprite_info>&& sprites);
	void add_sprite_infos(std::vector<sprite_record>& result, std::vector<sprite_info>&& sprites, size_t page_id);
}  // namespace lego

namespace lego {
	bool pixels_are_similar(surface_t const& left, surface_t const& right) {
		if (left.width != right.width || left.height != right.height) {
			return false;
		}

		auto const area = left.width * left.height;
		if (area == 0) {
			return true;
		}

		unsigned long long summed_error{};
		static constexpr auto max_ull = std::numeric_limits<unsigned long long>::max();

		for (int y = 0; y < left.height; ++y) {
			auto lhs = reinterpret_cast<uint32_t const*>(left.buffer + y * left.pitch);
			auto rhs = reinterpret_cast<uint32_t const*>(right.buffer + y * right.pitch);
			for (int x = 0; x < left.width; ++x, ++lhs, ++rhs) {
				auto const error = static_cast<unsigned long long>(clr_diff(*lhs, *rhs));
				if ((max_ull - error) <= summed_error) {
					[[unlikely]];
					summed_error = max_ull;
				} else {
					summed_error += error;
				}
			}
		}

		auto const error_per_pixel = summed_error / area;
		return error_per_pixel <= max_rgb_error;
	}

	surface_t view_from(surface_t const& surface, int x, int y, int width, int height) {
		if (surface.width <= width) {
			x = 0;
			width = surface.width;
		}
		if (surface.height <= height) {
			y = 0;
			height = surface.height;
		}

		return surface.subview(x, y, width, height);
	}

	static inline int abs_diff(int a, int b) { return a > b ? a - b : b - a; }

	bool is_similar(backed_surface_t& left, backed_surface_t& right) {
		static constexpr int offsets[] = {0, 1, 2, 3, 4};
		static constexpr auto max_offset = offsets[std::size(offsets) - 1];

		auto const diff_width = abs_diff(left.width, right.width);
		auto const diff_height = abs_diff(left.height, right.height);
		if (diff_width > max_offset || diff_height > max_offset) return false;

		auto const lhs = to_surface(left);
		auto const rhs = to_surface(right);

		auto const width_offsets = std::span<int const>{offsets, static_cast<size_t>(diff_width) + 1};
		auto const height_offsets = std::span<int const>{offsets, static_cast<size_t>(diff_height) + 1};

		for (auto y : height_offsets) {
			for (auto x : width_offsets) {
				auto const checked_width = std::min(lhs.width, rhs.width);
				auto const checked_height = std::min(lhs.height, rhs.height);
				if (pixels_are_similar(view_from(lhs, x, y, checked_width, checked_height),
				                       view_from(rhs, x, y, checked_width, checked_height))) {
					return true;
				}
			}
		}

		return false;
	}

	inline backed_surface_t& get_sprite(sprite_info& info) { return info.pixels; }
	inline backed_surface_t& get_sprite(sprite_record& row) { return get_sprite(row.key); }

	template <typename Collection>
	auto locate(Collection& pixmaps, backed_surface_t& rhs)
	    -> std::remove_reference_t<decltype(*std::begin(pixmaps))>* {
		for (auto& pixmap : pixmaps) {
			if (is_similar(get_sprite(pixmap), rhs)) return &pixmap;
		}

		return nullptr;
	}

	void add_sprite_infos(std::vector<sprite_info>& result, std::vector<sprite_info>&& sprites) {
		result.reserve(result.size() + sprites.size());

		for (auto& sprite : sprites) {
			if (locate(result, get_sprite(sprite))) continue;
			result.push_back(std::move(sprite));
		}
	}

	void add_sprite_infos(std::vector<sprite_record>& result, std::vector<sprite_info>&& sprites, size_t page_id) {
		result.reserve(result.size() + sprites.size());

		for (auto& sprite : sprites) {
			auto const ptr = locate(result, get_sprite(sprite));
			if (ptr) {
				ptr->pages.insert(page_id);
				continue;
			}
			result.push_back(sprite_record{.key = std::move(sprite), .pages = {page_id}});
		}
	}
}  // namespace lego

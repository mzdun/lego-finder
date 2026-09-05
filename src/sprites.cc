// Copyright (c) 2026 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

export module bricks:sprites;

import :pyxel;
import std;

export namespace bricks {
	struct sprite_info {
		backed_surface_t pixels{};
		uint32_t color{};

		std::string identifier(size_t ref, int digits, std::string_view ext = {}) const {
			auto const R = (color >> 4) & 0xf;
			auto const G = (color >> 12) & 0xf;
			auto const B = (color >> 20) & 0xf;
			return std::format("{:x}{:x}{:x}-{:0{}}{}", R, G, B, ref, digits, ext);
		}

		void store(std::filesystem::path const& filename) {
			auto const src = to_surface(pixels);
			auto const pixmap = pyxel::make_pixmap(src.width, src.height);
			to_surface(pixmap).copy(0, 0, src);
			pyxel::store_pixmap(filename, pixmap);
		}
	};

	struct sprite_placement {
		rect pos;
		uint32_t marker;
	};

	sprite_info cut_sprite(surface_t const& flooded,
	                       surface_t const& orig,
	                       rect const& pos,
	                       uint32_t marker,
	                       uint32_t background);
	rect fill_bom_area(surface_t const& surface, int orig_x, int orig_y, uint32_t color);
	sprite_placement fill_sprite_area(surface_t const& copy, int orig_x, int orig_y, uint32_t color);

	struct sprite_sorter {
		unsigned value{};       // sort darker blocks before lighter
		unsigned saturation{};  // sort dimmer blocks before colorful
		unsigned hue{};         // sort in roygbiv order
		int width{};
		int height{};
		size_t original_pos{};

		constexpr auto operator<=>(sprite_sorter const&) const noexcept = default;

		static sprite_sorter from(sprite_info const& key, size_t original_pos) {
			auto const [hue, saturation, value] = conv(key.color);
			auto const width = key.pixels.width;
			auto const height = key.pixels.height;
			return {
			    .value = value,
			    .saturation = saturation,
			    .hue = hue,
			    .width = width,
			    .height = height,
			    .original_pos = original_pos,
			};
		}

	private:
		struct HSV {
			unsigned h{};
			unsigned s{};
			unsigned v{};
		};

		static unsigned percent(double d) { return static_cast<unsigned>((d * 100.0) + .5); }

		static HSV conv(uint32_t color) {
			// scale down to 16 values per channel
			auto const R = (color >> 0) & 0xf0;
			auto const G = (color >> 8) & 0xf0;
			auto const B = (color >> 16) & 0xf0;

			auto const r = static_cast<double>(R) / 255.0;
			auto const g = static_cast<double>(G) / 255.0;
			auto const b = static_cast<double>(B) / 255.0;

			auto min_val = std::min({r, g, b});
			auto max_val = std::max({r, g, b});
			auto delta = max_val - min_val;

			HSV result{};
			result.v = percent(max_val);

			if (delta < 0.00001) {
				result.s = 0;
				result.h = 0;  // Undefined, usually 0
				return result;
			}

			if (max_val > 0.0) {
				result.s = percent(delta / max_val);  // Saturation
			} else {
				result.s = 0;
				result.h = 0;
				return result;
			}

			double h{};
			if (r == max_val) {
				h = (g - b) / delta;  // Between yellow & magenta
			} else if (g == max_val) {
				h = 2.0 + (b - r) / delta;  // Between cyan & yellow
			} else {
				h = 4.0 + (r - g) / delta;  // Between magenta & cyan
			}

			h *= 60.0;  // Convert to degrees [0, 360]
			if (h < 0.0f) {
				h += 360.0;
			}
			result.h = static_cast<unsigned>(h + .5);

			return result;
		}
	};
}  // namespace bricks

namespace bricks {
	class block_color_finder {
	public:
		void add(uint32_t current) {
			auto found = false;
			for (auto& [key, value] : histogram) {
				if (key == current) {
					++value;
					found = true;
					continue;
				}
				if (color_matches(key, current)) {
					++value;
				}
			}
			if (!found) ++histogram[current];
		}

		uint32_t most_popular() const noexcept {
			size_t max{};
			for (auto const& [key, count] : histogram) {
				if (count > max) max = count;
			}
			uint32_t R{}, G{}, B{}, counter{};
			for (auto const& [key, count] : histogram) {
				if (count != max) continue;

				R += (key >> 0) & 0xff;
				G += (key >> 8) & 0xff;
				B += (key >> 16) & 0xff;
				++counter;
			}

			if (counter) {
				R = clamp(R / counter);
				G = clamp(G / counter);
				B = clamp(B / counter);
			}
			return R | (G << 8) | (B << 16);
		}

	private:
		std::map<uint32_t, size_t> histogram{};
	};

	sprite_info cut_sprite(surface_t const& flooded,
	                       surface_t const& orig,
	                       rect const& pos,
	                       uint32_t marker,
	                       uint32_t background) {
		sprite_info result{};
		auto& [result_surface, color] = result;
		auto const sprite_mask = flooded.subview(pos.left, pos.top, pos.width(), pos.height());
		result_surface = orig.subview(pos.left, pos.top, pos.width(), pos.height()).clone();
		auto const sprite = to_surface(result_surface);

		block_color_finder colors{};

		for (auto y = 0; y < sprite_mask.height; ++y) {
			auto mask_row = reinterpret_cast<uint32_t const*>(sprite_mask.buffer + y * sprite_mask.pitch);
			auto row = reinterpret_cast<uint32_t*>(sprite.buffer + y * sprite.pitch);
			for (auto x = 0; x < sprite_mask.width; ++x, ++mask_row, ++row) {
				if (*mask_row != marker)
					*row = background;
				else
					colors.add(*row);
			}
		}

		color = colors.most_popular();
		return result;
	}

	struct point {
		int x{};
		int y{};
	};

	rect flood_fill(surface_t const& surface, int orig_x, int orig_y, uint32_t color, uint32_t fill, auto&& validator) {
		rect result{.left{orig_x}, .top{orig_y}, .right{orig_x}, .bottom{orig_y}};

		std::queue<point> points{};
		points.push(point{.x = orig_x, .y = orig_y});

		while (!points.empty()) {
			auto const [x, y] = points.front();
			points.pop();

			auto const row = reinterpret_cast<uint32_t*>(surface.buffer + y * surface.pitch);
			if (!validator(row[x], color)) continue;

			if (x < result.left) result.left = x;
			if (y < result.top) result.top = y;
			if (x > result.right) result.right = x;
			if (y > result.bottom) result.bottom = y;

			row[x] = fill;
			if (x) points.push({.x = x - 1, .y = y});
			if (y) points.push({.x = x, .y = y - 1});
			if (x < (surface.width - 1)) points.push({.x = x + 1, .y = y});
			if (y < (surface.height - 1)) points.push({.x = x, .y = y + 1});
		}

		return result;
	}

	static constexpr uint32_t bom_base = 0x00FFFFFF;

	rect fill_bom_area(surface_t const& surface, int orig_x, int orig_y, uint32_t color) {
		return flood_fill(surface, orig_x, orig_y, color, bom_base, color_matches);
	}

	sprite_placement fill_sprite_area(surface_t const& copy, int orig_x, int orig_y, uint32_t color) {
		return {.pos = flood_fill(copy, orig_x, orig_y, 0, color,
		                          [](auto current, auto) { return ((current >> 24) & 0xFF) == 0xFF; }),
		        .marker = color};
	}
}  // namespace bricks

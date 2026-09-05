// Copyright (c) 2026 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

module;

#include <SDL3/SDL_surface.h>
#include <SDL3_image/SDL_image.h>

export module lego:pyxel;

import std;

export namespace pyxel {
	struct init_pixmap_loader {
		init_pixmap_loader();
		~init_pixmap_loader();
	};

	struct surface_deleter {
		void operator()(SDL_Surface* surface) { SDL_DestroySurface(surface); }
	};
	using surface_ptr = std::unique_ptr<SDL_Surface, surface_deleter>;

	surface_ptr load_pixmap(std::filesystem::path const& path) noexcept;
	surface_ptr make_pixmap(int width, int height) noexcept;
	void store_pixmap(std::filesystem::path const& path, surface_ptr const& surface) noexcept;

	uint32_t map_rgb(uint8_t R, uint8_t G, uint8_t B, SDL_PixelFormat format = SDL_PIXELFORMAT_RGBA32) {
		return SDL_MapRGB(SDL_GetPixelFormatDetails(format), nullptr, R, G, B);
	}
}  // namespace pyxel

export namespace lego {
	constexpr uint32_t max_rgb_error = 30u;

	struct rect {
		int left{};
		int top{};
		int right{};
		int bottom{};

		constexpr auto operator<=>(rect const&) const noexcept = default;
		constexpr auto width() const noexcept { return right - left; }
		constexpr auto height() const noexcept { return bottom - top; }

		constexpr bool contains(int x, int y) const noexcept {
			return x >= left && x <= right && y >= top && y <= bottom;
		}
	};

	struct backed_surface_t {
		int width{};
		int height{};
		int bytes_per_pixel{};
		int pitch{};
		std::vector<char> buffer{};
	};

	struct surface_t {
		int width{};
		int height{};
		int bytes_per_pixel{};
		int width_in_bytes{width * bytes_per_pixel};
		int pitch{};
		char* buffer{};

		bool empty() const noexcept { return !width || !height; }
		surface_t subview(int x, int y, int new_width, int new_height) const;
		backed_surface_t clone() const;
		void copy(int x, int y, surface_t const& src);
	};

	surface_t to_surface(pyxel::surface_ptr const& surface);
	surface_t to_surface(backed_surface_t& backed);

	uint32_t clr_diff(uint32_t lhs, uint32_t rhs, unsigned offset);

	inline uint32_t clr_diff(uint32_t lhs, uint32_t rhs) {
		return clr_diff(lhs, rhs, 0) + clr_diff(lhs, rhs, 8) + clr_diff(lhs, rhs, 16);
	}

	inline uint32_t clamp(uint32_t value) { return std::min<uint32_t>(255, value); }

	inline bool color_matches(uint32_t lhs, uint32_t rhs) {
		if (lhs == rhs) return true;
		return clr_diff(lhs, rhs) <= max_rgb_error;
	}
}  // namespace lego

namespace pyxel {
	init_pixmap_loader::init_pixmap_loader() {
		if (!SDL_Init(0)) {
			auto const msg = std::format("SDL_Init error: {}", SDL_GetError());
			std::print(std::cerr, "{}\n", msg);
			throw std::runtime_error(msg);
		}
	}

	init_pixmap_loader::~init_pixmap_loader() {
		SDL_QuitSubSystem(0);
		SDL_Quit();
	}

	surface_ptr load_pixmap(std::filesystem::path const& path) noexcept {
		auto orig = surface_ptr{IMG_Load(path.string().c_str())};
		return surface_ptr{SDL_ConvertSurface(orig.get(), SDL_PIXELFORMAT_RGBA32)};
	}

	surface_ptr make_pixmap(int width, int height) noexcept {
		return surface_ptr{SDL_CreateSurface(width, height, SDL_PIXELFORMAT_RGBA32)};
	}

	void store_pixmap(std::filesystem::path const& path, surface_ptr const& surface) noexcept {
		IMG_SavePNG(surface.get(), path.string().c_str());
	}
}  // namespace pyxel

namespace lego {
	surface_t to_surface(pyxel::surface_ptr const& surface) {
		return {
		    .width = surface->w,
		    .height = surface->h,
		    .bytes_per_pixel = SDL_GetPixelFormatDetails(surface->format)->bytes_per_pixel,
		    .pitch = surface->pitch,
		    .buffer = reinterpret_cast<char*>(surface->pixels),
		};
	}
	surface_t to_surface(backed_surface_t& surface) {
		return {
		    .width = surface.width,
		    .height = surface.height,
		    .bytes_per_pixel = surface.bytes_per_pixel,
		    .pitch = surface.pitch,
		    .buffer = surface.buffer.data(),
		};
	}

	surface_t surface_t::subview(int x, int y, int new_width, int new_height) const {
		if (x < 0) {
			new_width += x;
			x = 0;
		}
		if (y < 0) {
			new_height += y;
			y = 0;
		}

		if (x >= width || y >= height) {
			return {
			    .bytes_per_pixel{bytes_per_pixel},
			    .pitch{pitch},
			};
		}

		if ((x + new_width) > width) {
			new_width = width - x;
		}

		if ((y + new_height) > height) {
			new_height = height - y;
		}

		return {
		    .width{new_width},
		    .height{new_height},
		    .bytes_per_pixel{bytes_per_pixel},
		    .pitch{pitch},
		    .buffer{buffer + y * pitch + x * bytes_per_pixel},
		};
	}

	backed_surface_t surface_t::clone() const {
		auto result = backed_surface_t{
		    .width{width},
		    .height{height},
		    .bytes_per_pixel{bytes_per_pixel},
		    .pitch{width_in_bytes},
		    .buffer{},
		};

		result.buffer.resize(height * width_in_bytes);
		to_surface(result).copy(0, 0, *this);

		return result;
	}

	void surface_t::copy(int x, int y, surface_t const& src) {
		if (bytes_per_pixel != src.bytes_per_pixel) {
			return;
		}

		auto target = subview(x, y, src.width, src.height);
		auto source = src.subview(x < 0 ? -x : 0, y < 0 ? -y : 0, target.width, target.height);
		target = target.subview(0, 0, source.width, source.height);

		if (source.empty() || target.empty()) {
			return;
		}

		auto tgt_ptr = target.buffer;
		auto src_ptr = source.buffer;

		for (auto row_index = 0; row_index < target.height; ++row_index) {
			std::memcpy(tgt_ptr, src_ptr, static_cast<size_t>(target.width_in_bytes));
			tgt_ptr += target.pitch;
			src_ptr += source.pitch;
		}
	}

	static inline int abs_diff(int a, int b) { return a > b ? a - b : b - a; }

	uint32_t clr_diff(uint32_t lhs, uint32_t rhs, unsigned offset) {
		return abs_diff((lhs >> offset) & 0xff, (rhs >> offset) & 0xff);
	}
}  // namespace lego

// Copyright (c) 2026 Marcin Zdun
// This code is licensed under MIT license (see LICENSE for details)

export module lego;

import :args;
import :pyxel;
import :pool;
import :db;
import std;

using namespace std::literals;

namespace lego {
	export int tool(::args::args_view const& arguments);
}  // namespace lego

module :private;

namespace lego {
	namespace {
		static constexpr auto ident_digits = 5;
		static constexpr auto error_while_removing = static_cast<std::uintmax_t>(-1);
		static constexpr auto sprites_dirname = "html/sprites"sv;
		static constexpr auto pages_dirname = "html/pages"sv;
		static constexpr auto index_dirname = "html"sv;

		std::mutex printing_mutex{};

		void print(std::string_view message) {
			std::lock_guard the{printing_mutex};
			std::print("{}\n", message);
		}

		void make_directory(std::filesystem::path const& dirname) {
			std::error_code ec{};
			create_directories(dirname, ec);
			if (ec) {
				std::print(std::cerr, "{}: error: cannot create directory `{}`\n", version::program,
				           as_sv(dirname.generic_u8string()));
				return;
			}
		}

		void process_page(struct task_context& ctx, size_t page_id, pyxel::surface_ptr const& pixmap);

		struct task_context {
			std::mutex db_mutex{};
			std::vector<std::filesystem::path> filelist{};
			std::vector<sprite_record> db{};
			std::filesystem::path report_dir{};
			std::vector<sprite_sorter> sorted{};
			uint32_t color{};

			void clean_report() {
				std::error_code ec{};
				auto const removed_count = std::filesystem::remove_all(report_dir, ec);
				if (removed_count == error_while_removing) {
					std::print(std::cerr, "{}: error: cannot remove output directory {}\n", version::program,
					           as_sv(report_dir.generic_u8string()));
				}
			}

			void list_pages(std::filesystem::path const& dirname) {
				size_t count = 0;
				enum_pages(dirname, [&](auto const&) { ++count; });

				filelist.clear();
				filelist.reserve(count);
				enum_pages(dirname, [&](auto const& entry) { filelist.emplace_back(entry.path()); });
			}

			void load_pages(int pages_count_int) {
				auto const pages_count = std::min(
				    pages_count_int < 0 ? filelist.size() : static_cast<size_t>(pages_count_int), filelist.size());

				size_t page_id{0};
				for (auto& filename : filelist) {
					if (page_id == pages_count) break;
					post_task(task::loading, [&filename, this, page_id]() { load_page(page_id, filename); });
					++page_id;
				}
			}

			void load_page(size_t page_id, std::filesystem::path const& filename) {
				auto pixmap = pyxel::load_pixmap(filename);

				print(std::format("-- [{}] read {}", page_id, as_sv(filename.generic_u8string())));

				post_task(task::processing, [=, this, raw = pixmap.release()] {
					pyxel::surface_ptr pixmap{raw};
					process_page(*this, page_id, pixmap);
				});
			}

			void add_sprites(size_t page_id, std::vector<sprite_info>& sprites) {
				std::lock_guard the{db_mutex};
				add_sprite_infos(db, std::move(sprites), page_id);
			}

			void write_report() {
				sort_sprites();

				post_task(task::postprocessing, [this] { store_sprites(); });
				post_task(task::postprocessing, [this] {
					auto const dirname = report_dir / index_dirname;
					make_directory(dirname);

					write_booklet(dirname);
					write_index(dirname, "LEGO Reverse Brick search"sv);
					write_brick_pages();
					write_yaml();
				});
			}

		private:
			static void enum_pages(std::filesystem::path const& dirname, auto&& cb) {
				std::error_code ec{};
				auto it = std::filesystem::directory_iterator{dirname, ec};
				if (ec) {
					std::print("{}: error {}: cannot list files in {}: {}\n", version::program, ec.value(),
					           as_sv(dirname.generic_u8string()), ec.message());
					std::exit(1);
				}

				for (auto const& entry : it) {
					if (entry.path().extension() != u8".png"sv) continue;
					cb(entry);
				}
			}

			void sort_sprites() {
				sorted.clear();
				sorted.reserve(db.size());

				size_t id{};
				for (auto& [info, pages] : db) {
					sorted.push_back(sprite_sorter::from(info, id));
					++id;
				}

				std::sort(sorted.begin(), sorted.end());
			}

			void store_sprites() {
				auto const sprites_dir = report_dir / sprites_dirname;
				make_directory(sprites_dir);

				size_t id{};
				for (auto& [info, pages] : db) {
					info.store(sprites_dir / info.identifier(id, ident_digits, ".png"sv));
					++id;
				}

				print("-- Write sprites"sv);
			}

			void write_yaml() {
				std::ofstream index_yaml{report_dir / "index.yaml"sv};
				for (auto const& id : sorted) {
					auto const& ref = db[id.original_pos];

					std::print(index_yaml, "{}:\n", ref.key.identifier(id.original_pos, ident_digits));
					for (auto const page : ref.pages) {
						std::print(index_yaml, "  - {}\n", as_sv(filelist[page].generic_u8string()));
					}
				}
				print("-- Write index.yaml"sv);
			}

			static void write_header(std::ofstream& output, std::string_view title, std::string_view style) {
				std::print(output, R"(<html>
<head>
	<title>{}</title>
	<style>{}</style>
</head>
<body>
)",
				           title, style);
			}

			static void write_footer(std::ofstream& output) { std::print(output, "</body>\n</html>\n"); }

			void write_index(std::filesystem::path const& dirname, std::string_view title) {
				auto const R = (color >> 0) & 0xff;
				auto const G = (color >> 8) & 0xff;
				auto const B = (color >> 16) & 0xff;

				std::ofstream output{dirname / "index.html"sv};
				write_header(output, title,
				             std::format(R"(
		body {{ background: #{:02x}{:02x}{:02x}; }}
		img {{
			padding: 2em;
			height: auto; 
			width: auto; 
			max-width: 75px; 
			max-height: 75px;
		}}
)",
				                         R, G, B));
				for (auto const& id : sorted) {
					std::print(output, "	<a href=\"pages/{0}.html\"><img src=\"sprites/{0}.png\"></a>\n",
					           db[id.original_pos].key.identifier(id.original_pos, ident_digits));
				}
				write_footer(output);

				print("-- Write index.html"sv);
			}

			void write_brick_page(std::filesystem::path const& dirname, size_t original_pos) {
				auto const& [info, pages] = db[original_pos];

				std::ofstream output{dirname / info.identifier(original_pos, ident_digits, ".html"sv)};
				write_header(output, "Brick Pages Listing"sv, R"(
		body { padding: 0; margin: 0; }
		img {
			padding: 0;
			padding-bottom: 1em;
			height: auto;
			width: auto; 
			max-width: 100vw;
			max-height: 100vh;
		}
)"sv);

				for (auto const& page : pages) {
					std::print(output, "<a href=\"../booklet.html#page-{}\"><img src=\"{}\"></a>\n", page,
					           as_sv(filelist[page].generic_u8string()));
				}
				write_footer(output);
			}

			void write_brick_pages() {
				auto const dirname = report_dir / pages_dirname;
				make_directory(dirname);

				for (auto const& id : sorted) {
					write_brick_page(dirname, id.original_pos);
				}
				print("-- Write brick indexes"sv);
			}

			void write_booklet(std::filesystem::path const& dirname) {
				std::ofstream output{dirname / "booklet.html"sv};
				write_header(output, "Booklet"sv, R"(
		body { padding: 0; margin: 0; }
		img {
			padding: 0;
			padding-bottom: 1em;
			height: auto;
			width: auto; 
			max-width: 100vw;
			max-height: 100vh;
		}
)"sv);

				size_t page_id = 0;
				for (auto const& filename : filelist) {
					std::print(output, "<a id=\"page-{1}\" href=\"{0}\"><img src=\"{0}\"></a>\n",
					           as_sv(filename.generic_u8string()), page_id);
					++page_id;
				}
				write_footer(output);
				print("-- Write booklet.html"sv);
			}
		};

		uint8_t get_hex(std::string_view view) {
			unsigned result{};
			auto const data = view.data();
			auto const end = data + view.size();
			auto const [ptr, err] = std::from_chars(data, end, result, 16);
			if (ptr != end || err != std::errc{} || result > 0xff) return 0;
			return static_cast<uint8_t>(result);
		}

		uint8_t get_hex(char a, char b) {
			char s[] = {a, b};
			return get_hex(std::string_view{s, 2});
		}

		uint32_t parse_color(std::string_view view) {
			if (view.starts_with('#')) {
				view = view.substr(1);
			}

			if (view.size() == 3) {
				auto const r_char = view[0];
				auto const g_char = view[1];
				auto const b_char = view[2];

				return pyxel::map_rgb(get_hex(r_char, r_char), get_hex(g_char, g_char), get_hex(b_char, b_char));
			}

			if (view.size() == 6) {
				return pyxel::map_rgb(get_hex(view.substr(0, 2)), get_hex(view.substr(2, 2)),
				                      get_hex(view.substr(4, 2)));
			}

			return 0;
		}

		std::vector<sprite_info> process_sprites(surface_t const& orig, uint32_t background) {
			std::vector<sprite_placement> places{};
			auto backed = orig.clone();
			auto const flood_surface = to_surface(backed);

			for (auto y = 0; y < flood_surface.height; ++y) {
				auto const bom_row = reinterpret_cast<uint32_t const*>(flood_surface.buffer + y * flood_surface.pitch);
				for (auto x = 0; x < flood_surface.width; ++x) {
					if (!(bom_row[x] & 0xFF000000)) continue;
					places.push_back(fill_sprite_area(flood_surface, x, y, static_cast<uint32_t>(places.size())));
				}
			}

			std::vector<sprite_info> result{};
			result.reserve(places.size());
			for (auto const& [pos, marker] : places) {
				if (!pos.left || !pos.top || pos.right == (flood_surface.width - 1) ||
				    pos.bottom == (flood_surface.height - 1))
					continue;
				if (pos.width() < 40 || pos.height() < 40) continue;
				result.push_back(cut_sprite(flood_surface, orig, pos, marker, background));
			}
			return result;
		}

		void process_page(task_context& ctx, size_t page_id, pyxel::surface_ptr const& pixmap) {
			class rect_filter {
			public:
				bool seen(int x, int y) {
					for (auto const& rect : rects) {
						if (rect.contains(x, y)) {
							return true;
						}
					}
					return false;
				}

				void add(rect const& r) { rects.push_back(r); }

			private:
				std::vector<rect> rects{};
			};

			rect_filter filter{};
			std::vector<sprite_info> sprites{};

			auto const surface = to_surface(pixmap);
			for (auto y = 0; y < surface.height; ++y) {
				auto const row = reinterpret_cast<uint32_t const*>(surface.buffer + y * surface.pitch);
				for (auto x = 0; x < surface.width; ++x) {
					if (!color_matches(row[x], ctx.color) || filter.seen(x, y)) continue;

					auto const pos = fill_bom_area(surface, x, y, ctx.color);
					filter.add(pos);

					auto view = surface.subview(pos.left, pos.top, pos.width(), pos.height());
					add_sprite_infos(sprites, process_sprites(view, ctx.color));
				}
			}

			post_task(task::postprocessing,
			          [=, &ctx, sprites = std::move(sprites)] mutable { ctx.add_sprites(page_id, sprites); });
		}

		auto make_loop(unsigned threads) {
			return [threads](std::function<void()>&& refref) {
				post_task(task::loading, std::move(refref));
				run_tasks(threads);
			};
		}
	}  // namespace

	int tool(::args::args_view const& arguments) {
		auto [indir, outdir, color_str, threads, pages_count_int] = parse_cli(arguments);
		auto const run_loop = make_loop(threads);

		pyxel::init_pixmap_loader loader{};

		task_context ctx{.report_dir = std::move(outdir), .color = parse_color(color_str)};
		run_loop([&, pages_count_int] {
			ctx.clean_report();
			ctx.list_pages(indir);
			ctx.load_pages(pages_count_int);
		});

		std::print("-- Postprocessing\n");

		run_loop([&] { ctx.write_report(); });

		return 0;
	}
}  // namespace lego

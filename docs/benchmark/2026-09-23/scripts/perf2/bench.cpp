// shashoku の常駐・同時生成コストの計測。公開 API だけを使う。
// 使い方:
//   bench resident  <out.json>
//   bench longrun   <case> <iters> <out.json>
//   bench concurrent <N> <out.json>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "shashoku/shashoku.hpp"

namespace {

const char* kExp =
    "/tmp/claude-1000/-home-junya-src-github-com-junyaU-shashoku/"
    "94c9fae6-a9a0-40c9-ac43-94fa14abccb2/scratchpad/exp";

struct Case {
  const char* name;
  int width;
  int height;  // 0 = 内容に追従（未指定）
  bool image;  // case06 だけ
};

const std::vector<Case>& cases() {
  static const std::vector<Case> kCases = {
      {"case01", 900, 0, false},     {"case02", 1000, 0, false},   {"case03", 720, 0, false},
      {"case04", 800, 0, false},     {"case05", 1080, 1080, false}, {"case06", 1200, 630, true},
      {"case07", 1080, 1080, false}, {"case08", 800, 0, false},    {"case09", 800, 0, false},
      {"case10", 800, 1200, false},
  };
  return kCases;
}

std::vector<std::uint8_t> read_bytes(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    std::cerr << "cannot open: " << path << '\n';
    std::exit(2);
  }
  return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
}

std::string read_text(const std::string& path) {
  const auto b = read_bytes(path);
  return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

// /proc/self/status から kB 単位の値をバイトで返す
long long proc_status_kb(const char* key) {
  std::ifstream f("/proc/self/status");
  std::string line;
  const std::string prefix = std::string(key) + ":";
  while (std::getline(f, line)) {
    if (line.rfind(prefix, 0) == 0) {
      std::istringstream is(line.substr(prefix.size()));
      long long v = 0;
      is >> v;
      return v * 1024;
    }
  }
  return -1;
}

double median(std::vector<double> v) {
  if (v.empty()) return 0.0;
  std::sort(v.begin(), v.end());
  const std::size_t n = v.size();
  if (n % 2 == 1) return v[n / 2];
  return (v[n / 2 - 1] + v[n / 2]) / 2.0;
}

shashoku::RenderOptions opts_for(const Case& c) {
  shashoku::RenderOptions o;
  o.viewport_width = c.width;
  if (c.height > 0) o.viewport_height = c.height;
  // compression_level は既定 6 のまま
  return o;
}

struct Inputs {
  shashoku::LoadedFonts fonts;
  shashoku::LoadedImages images;
  std::vector<std::string> html;  // cases() と同じ並び
};

shashoku::LoadedFonts load_fonts() {
  shashoku::FontSet fs;
  // CLI と同じ順（欧文 → 和文ではなく、実験で両者に渡した 3 本の順）
  const std::string dir = std::string(kExp) + "/bin/fonts/";
  const auto a = read_bytes(dir + "NotoSansJP-Regular.otf");
  const auto b = read_bytes(dir + "NotoSansJP-Bold.otf");
  const auto c = read_bytes(dir + "NotoSans-Regular.ttf");
  fs.add(a);
  fs.add(b);
  fs.add(c);
  auto r = shashoku::LoadedFonts::prepare(fs);
  if (!r) {
    std::cerr << "font prepare failed: " << shashoku::to_string(r.error()) << '\n';
    std::exit(3);
  }
  return std::move(*r);
}

shashoku::LoadedImages load_images() {
  shashoku::ImageSet is;
  const auto avatar = read_bytes(std::string(kExp) + "/bin/assets/avatar.png");
  is.add("avatar", avatar);
  auto r = shashoku::LoadedImages::prepare(is);
  if (!r) {
    std::cerr << "image prepare failed: " << shashoku::to_string(r.error()) << '\n';
    std::exit(3);
  }
  return std::move(*r);
}

std::vector<std::string> load_html() {
  std::vector<std::string> out;
  for (const auto& c : cases()) {
    out.push_back(read_text(std::string(kExp) + "/b_guided/" + c.name + ".html"));
  }
  return out;
}

// 1 枚描く。失敗したら即終了（fail loudly）
struct Rendered {
  int width;
  int height;
  std::size_t png_bytes;
  std::size_t warnings;
};

Rendered render_one(const Inputs& in, std::size_t idx) {
  const auto& c = cases()[idx];
  const auto o = opts_for(c);
  auto r = c.image ? shashoku::render(in.html[idx], in.fonts, in.images, o)
                   : shashoku::render(in.html[idx], in.fonts, o);
  if (!r) {
    std::cerr << "render failed (" << c.name << "): " << shashoku::to_string(r.error()) << '\n';
    std::exit(4);
  }
  return {r->width, r->height, r->png.size(), r->warnings.size()};
}

void run_resident(const std::string& out_path) {
  const long long rss_start = proc_status_kb("VmRSS");
  Inputs in{load_fonts(), load_images(), load_html()};
  const long long rss_after_prepare = proc_status_kb("VmRSS");

  std::ostringstream js;
  js << "{\n  \"mode\": \"resident\",\n  \"warmup\": 3,\n  \"iters\": 20,\n  \"cases\": [\n";
  for (std::size_t i = 0; i < cases().size(); ++i) {
    Rendered last{0, 0, 0, 0};
    for (int w = 0; w < 3; ++w) last = render_one(in, i);
    std::vector<double> ms;
    ms.reserve(20);
    for (int k = 0; k < 20; ++k) {
      const auto t0 = std::chrono::steady_clock::now();
      last = render_one(in, i);
      const auto t1 = std::chrono::steady_clock::now();
      ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    std::vector<double> sorted = ms;
    std::sort(sorted.begin(), sorted.end());
    js << "    {\"case\": \"" << cases()[i].name << "\", \"width\": " << last.width
       << ", \"height\": " << last.height << ", \"png_bytes\": " << last.png_bytes
       << ", \"warnings\": " << last.warnings << ", \"median_ms\": " << median(ms)
       << ", \"min_ms\": " << sorted.front() << ", \"max_ms\": " << sorted.back()
       << ", \"rss_after_bytes\": " << proc_status_kb("VmRSS") << "}";
    js << (i + 1 == cases().size() ? "\n" : ",\n");
  }
  js << "  ],\n";
  js << "  \"rss_start_bytes\": " << rss_start << ",\n";
  js << "  \"rss_after_prepare_bytes\": " << rss_after_prepare << ",\n";
  js << "  \"rss_end_bytes\": " << proc_status_kb("VmRSS") << ",\n";
  js << "  \"vmhwm_bytes\": " << proc_status_kb("VmHWM") << "\n}\n";
  std::ofstream(out_path) << js.str();
  std::cout << js.str();
}

void run_longrun(const std::string& case_name, int iters, const std::string& out_path) {
  std::size_t idx = 0;
  bool found = false;
  for (std::size_t i = 0; i < cases().size(); ++i) {
    if (case_name == cases()[i].name) {
      idx = i;
      found = true;
    }
  }
  if (!found) {
    std::cerr << "unknown case: " << case_name << '\n';
    std::exit(2);
  }
  const long long rss_start = proc_status_kb("VmRSS");
  Inputs in{load_fonts(), load_images(), load_html()};
  const long long rss_after_prepare = proc_status_kb("VmRSS");

  std::ostringstream js;
  js << "{\n  \"mode\": \"longrun\",\n  \"case\": \"" << case_name << "\",\n  \"iters\": " << iters
     << ",\n  \"samples\": [\n";
  std::vector<double> ms;
  ms.reserve(static_cast<std::size_t>(iters));
  const auto tstart = std::chrono::steady_clock::now();
  bool first_sample = true;
  for (int k = 1; k <= iters; ++k) {
    const auto t0 = std::chrono::steady_clock::now();
    render_one(in, idx);
    const auto t1 = std::chrono::steady_clock::now();
    ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    if (k % 100 == 0 || k == 1) {
      if (!first_sample) js << ",\n";
      first_sample = false;
      js << "    {\"n\": " << k << ", \"rss_bytes\": " << proc_status_kb("VmRSS")
         << ", \"vmhwm_bytes\": " << proc_status_kb("VmHWM") << "}";
    }
  }
  const auto tend = std::chrono::steady_clock::now();
  js << "\n  ],\n";
  js << "  \"median_ms\": " << median(ms) << ",\n";
  js << "  \"total_s\": " << std::chrono::duration<double>(tend - tstart).count() << ",\n";
  js << "  \"rss_start_bytes\": " << rss_start << ",\n";
  js << "  \"rss_after_prepare_bytes\": " << rss_after_prepare << ",\n";
  js << "  \"rss_end_bytes\": " << proc_status_kb("VmRSS") << ",\n";
  js << "  \"vmhwm_bytes\": " << proc_status_kb("VmHWM") << "\n}\n";
  std::ofstream(out_path) << js.str();
  std::cout << js.str();
}

void run_concurrent(int n_threads, int rounds, const std::string& out_path) {
  Inputs in{load_fonts(), load_images(), load_html()};
  const long long rss_after_prepare = proc_status_kb("VmRSS");

  // ウォームアップ: 1 スレッドで全ケースを 1 周
  for (std::size_t i = 0; i < cases().size(); ++i) render_one(in, i);

  std::atomic<long long> done{0};
  const auto t0 = std::chrono::steady_clock::now();
  std::vector<std::thread> ts;
  ts.reserve(static_cast<std::size_t>(n_threads));
  for (int t = 0; t < n_threads; ++t) {
    ts.emplace_back([&in, rounds, &done]() {
      long long local = 0;
      for (int r = 0; r < rounds; ++r) {
        for (std::size_t i = 0; i < cases().size(); ++i) {
          render_one(in, i);
          ++local;
        }
      }
      done.fetch_add(local, std::memory_order_relaxed);
    });
  }
  for (auto& t : ts) t.join();
  const auto t1 = std::chrono::steady_clock::now();
  const double secs = std::chrono::duration<double>(t1 - t0).count();
  const long long pages = done.load();

  std::ostringstream js;
  js << "{\n  \"mode\": \"concurrent\",\n  \"threads\": " << n_threads
     << ",\n  \"rounds\": " << rounds << ",\n  \"pages\": " << pages
     << ",\n  \"wall_s\": " << secs << ",\n  \"pages_per_sec\": " << (pages / secs)
     << ",\n  \"rss_after_prepare_bytes\": " << rss_after_prepare
     << ",\n  \"rss_end_bytes\": " << proc_status_kb("VmRSS")
     << ",\n  \"vmhwm_bytes\": " << proc_status_kb("VmHWM") << "\n}\n";
  std::ofstream(out_path) << js.str();
  std::cout << js.str();
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: bench resident <out.json> | bench longrun <case> <iters> <out.json> | "
                 "bench concurrent <N> <rounds> <out.json>\n";
    return 2;
  }
  const std::string mode = argv[1];
  if (mode == "resident" && argc == 3) {
    run_resident(argv[2]);
  } else if (mode == "longrun" && argc == 5) {
    run_longrun(argv[2], std::atoi(argv[3]), argv[4]);
  } else if (mode == "concurrent" && argc == 5) {
    run_concurrent(std::atoi(argv[2]), std::atoi(argv[3]), argv[4]);
  } else {
    std::cerr << "bad arguments\n";
    return 2;
  }
  return 0;
}

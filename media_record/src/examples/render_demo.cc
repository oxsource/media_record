// render_demo.cc
//
// Standalone debug entry for media::record::render::DashcamRenderer. Renders a
// few frames and dumps them to PNG via native::ui Surface::CreateFromPixels +
// Surface::Dump, so the composited output (background + bouncing dog +
// timestamp) can be inspected without running the full graph pipeline.

#include <cerrno>
#include <cstdio>
#include <ctime>
#include <string>

#include <sys/stat.h>

#include "native_ui/surface.h"
#include "src/render/dashcam_renderer.h"

namespace {

std::string NowTimestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tm_buf{};
  localtime_r(&t, &tm_buf);
  char buf[64];
  if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf) == 0) {
    return "1970-01-01 00:00:00";
  }
  return std::string(buf);
}

// Best-effort mkdir for the output parent directory (bazel run's cwd is the
// runfiles dir, so create it before writing PNGs).
bool MkdirParents(const std::string& path) {
  std::string cur;
  for (char c : path) {
    if (c == '/') {
      if (!cur.empty() && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
    }
    cur += c;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string background = "src/examples/assets/dashcam_road.png";
  const std::string dog = "src/examples/assets/flydog.png";

  // Optional --out=<dir> selects the output directory for the dumped PNGs.
  std::string out_dir = ".";
  for (int i = 1; i < argc; i++) {
    const std::string arg = argv[i];
    if (arg.rfind("--out=", 0) == 0) out_dir = arg.substr(6);
  }
  if (!out_dir.empty() && out_dir.back() != '/') out_dir += "/";
  if (!MkdirParents(out_dir)) {
    std::fprintf(stderr, "render_demo: cannot create output dir '%s'\n",
                 out_dir.c_str());
    return 1;
  }

  auto renderer =
      media::record::render::DashcamRenderer::Create(background, dog, 1280, 720);
  if (!renderer) {
    std::fprintf(stderr, "render_demo: failed to create renderer\n");
    return 1;
  }

  native::ui::PixelBuffer frame = native::ui::Surface::Allocate(
      renderer->width(), renderer->height(), native::ui::PixelFormat::kRGBA);
  if (frame.empty()) {
    std::fprintf(stderr, "render_demo: Surface::Allocate failed\n");
    return 1;
  }

  // Spread across one bounce cycle (period=30) so the bouncing dog's position
  // is visibly different between dumped frames: 0=ground, 7=apex, 15=ground.
  const int frames_to_dump[] = {0, 7, 15, 22};
  for (int frame_index : frames_to_dump) {
    const std::string ts = NowTimestamp();
    if (!renderer->Render(frame_index, ts, frame)) {
      std::fprintf(stderr, "render_demo: Render(%d) failed\n", frame_index);
      return 1;
    }

    auto surface = native::ui::Surface::CreateFromPixels(frame);
    if (!surface) {
      std::fprintf(stderr, "render_demo: CreateFromPixels failed (frame %d)\n",
                   frame_index);
      return 1;
    }

    char path[256];
    std::snprintf(path, sizeof(path), "%srender_demo_%03d.png",
                  out_dir.c_str(), frame_index);
    if (!surface->Dump(path)) {
      std::fprintf(stderr, "render_demo: Dump %s failed\n", path);
      return 1;
    }
    std::printf("render_demo: frame %d @ %s -> %s\n", frame_index, ts.c_str(),
                path);
  }

  std::printf("render_demo: done\n");
  return 0;
}

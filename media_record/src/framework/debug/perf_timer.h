// perf_timer.h
//
// Lightweight, always-on per-stage timing for the dashcam pipeline (debug /
// profiling aid). Each node timestamps the boundaries of its work (render,
// convert, encode, muxer push, ...) with a StageTimer, and prints a summary
// (total / count / avg / max, wall clock) in Close(). This is permanent
// instrumentation: it costs one steady_clock read per stage boundary and one
// line of stderr output per node at shutdown, and gives an immediate answer to
// "where does the time go" without re-instrumenting.
//
// The summary also reports which OS threads the node actually ran on (a set of
// std::thread::id), so you can verify pipeline parallelism: if every node's
// work happened on one thread, the graph was serialized despite executor
// config; distinct thread ids across render/encoder/muxer mean the pipeline is
// truly running in parallel.
//
// Usage:
//   StageTimer timer;
//   auto t0 = timer.Begin();           // e.g. start of Process
//   ...work...
//   timer.Accumulate("convert", t0);   // +elapsed under "convert"
//   ...
//   timer.PrintSummary("encoder");     // in Close()
//
// Thread-safety: used from a single node's Process/Close (graph_runtime runs
// each node on one thread at a time), so no locking is needed.

#ifndef MEDIA_RECORD_FRAMEWORK_DEBUG_PERF_TIMER_H_
#define MEDIA_RECORD_FRAMEWORK_DEBUG_PERF_TIMER_H_

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>

namespace media::record {

class StageTimer {
 public:
  using TimePoint = std::chrono::steady_clock::time_point;

  StageTimer() = default;

  TimePoint Begin() const { return std::chrono::steady_clock::now(); }

  // Accumulate the elapsed microseconds since `begin` into `stage`, and record
  // the thread this call ran on (the node's actual executing thread).
  void Accumulate(const char* stage, TimePoint begin) {
    NoteThread();
    const int64_t us = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - begin)
                           .count();
    Stage& s = stages_[stage];
    ++s.count;
    s.total_us += us;
    if (us > s.max_us) s.max_us = us;
  }

  // One-shot accumulate of an already-measured duration (us).
  void Add(const char* stage, int64_t us) {
    NoteThread();
    Stage& s = stages_[stage];
    ++s.count;
    s.total_us += us;
    if (us > s.max_us) s.max_us = us;
  }

  // Print one line per stage:  total(ms)  avg(us)  max(ms)  count  stage,
  // followed by the set of threads this node executed on (hex thread ids).
  void PrintSummary(const char* label) const {
    if (stages_.empty()) return;
    std::fprintf(stderr, "[%s] timing:\n", label);
    for (const auto& [name, s] : stages_) {
      if (s.count == 0) continue;
      std::fprintf(stderr,
                   "  %-10s  total %6lldms  avg %7lldus  max %6lldms  x%lld\n",
                   name.c_str(), static_cast<long long>(s.total_us / 1000),
                   static_cast<long long>(s.total_us / s.count),
                   static_cast<long long>(s.max_us / 1000),
                   static_cast<long long>(s.count));
    }
    if (!threads_.empty()) {
      std::fprintf(stderr, "  threads:");
      for (const std::thread::id& id : threads_) {
        std::fprintf(stderr, "  %s", ThreadIdToHex(id).c_str());
      }
      std::fprintf(stderr, "  (%zu thread%s)\n", threads_.size(),
                   threads_.size() == 1 ? "" : "s");
    }
  }

 private:
  struct Stage {
    int64_t total_us = 0;
    int64_t max_us = 0;
    int64_t count = 0;
  };

  void NoteThread() { threads_.insert(std::this_thread::get_id()); }

  static std::string ThreadIdToHex(std::thread::id id) {
    std::ostringstream oss;
    oss << id;
    return oss.str();
  }

  std::map<std::string, Stage> stages_;
  std::set<std::thread::id> threads_;
};

}  // namespace media::record

#endif  // MEDIA_RECORD_FRAMEWORK_DEBUG_PERF_TIMER_H_

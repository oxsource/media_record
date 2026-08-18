#include "media_record/media_record.h"

#include <cstdio>

// Minimal consumer used by task T015 (US1) to prove the public umbrella header
// compiles and links on its own. Skeleton-only: exercises NodeRegistry without
// pulling in the three capability repos.

int main() {
  media::record::NodeRegistry& reg = media::record::NodeRegistry::Instance();
  const bool has_unknown = reg.Contains("definitely-not-registered");
  std::printf("media_record consumer_demo: unknown node resolved=%d\n",
              has_unknown ? 1 : 0);
  return has_unknown ? 1 : 0;
}

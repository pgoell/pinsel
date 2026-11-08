#pragma once

#include <QJsonObject>
#include <optional>

namespace pinsel {

// What this process holds, in MiB. A field is empty where the platform does not
// say.
struct MemoryUse {
  // On Windows the private bytes: the commit charge, which also counts memory
  // the graphics kernel charges to this process without mapping it here.
  // Elsewhere the anonymous resident set.
  double private_mb = 0.0;
  double peak_private_mb = 0.0;
  // Private committed pages in this process's address space. Below private_mb
  // by what the graphics kernel charges (Windows only).
  std::optional<double> mapped_private_mb;
  // What malloc has handed out and not taken back.
  std::optional<double> heap_mb;
  // This process's video memory on the adapter where it uses the most, and how
  // much the OS lets it use there given the other processes (Windows only).
  std::optional<double> gpu_local_mb;
  std::optional<double> gpu_shared_mb;
  std::optional<double> gpu_budget_mb;
};

[[nodiscard]] MemoryUse measureMemory();
// Leaves out the empty fields.
[[nodiscard]] QJsonObject toJson(const MemoryUse& use);

}  // namespace pinsel

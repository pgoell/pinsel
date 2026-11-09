#include "app/memory.hpp"

#include <cstddef>
#include <optional>

#ifdef _WIN32
#include <windows.h>
// psapi.h and dxgi1_4.h need windows.h first.
#include <dxgi1_4.h>
#include <psapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <vector>
#else
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#endif

#if !defined(PINSEL_ASAN) && defined(__GLIBC__)
#include <malloc.h>
#endif

#ifdef PINSEL_ASAN
// From <sanitizer/allocator_interface.h>, which not every clang-tidy ships.
extern "C" std::size_t __sanitizer_get_current_allocated_bytes();
#endif

namespace pinsel {

namespace {

constexpr double kMiB = 1024.0 * 1024.0;

double toMiB(std::size_t bytes) {
  return static_cast<double>(bytes) / kMiB;
}

// AddressSanitizer serves every malloc from its own allocator, which the
// platform's heap never sees.
std::optional<double> heapInUse() {
#ifdef PINSEL_ASAN
  return toMiB(__sanitizer_get_current_allocated_bytes());
#elif defined(_WIN32)
  // The C runtime allocates from the process heap; Qt and the drivers may add
  // heaps of their own.
  std::vector<HANDLE> heaps(GetProcessHeaps(0, nullptr));
  heaps.resize(std::min<std::size_t>(
      heaps.size(), GetProcessHeaps(static_cast<DWORD>(heaps.size()), heaps.data())));
  std::size_t bytes = 0;
  for (HANDLE heap : heaps) {
    if (HeapLock(heap) == 0) {
      continue;
    }
    PROCESS_HEAP_ENTRY entry{};
    while (HeapWalk(heap, &entry) != 0) {
      if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
        bytes += entry.cbData;
      }
    }
    HeapUnlock(heap);
  }
  return toMiB(bytes);
#elif defined(__GLIBC__)
  const struct mallinfo2 info = mallinfo2();
  return toMiB(info.uordblks + info.hblkhd);
#else
  return std::nullopt;
#endif
}

#ifdef _WIN32

void readCounters(MemoryUse& use) {
  PROCESS_MEMORY_COUNTERS_EX counters{};
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                           sizeof(counters)) != 0) {
    use.private_mb = toMiB(counters.PrivateUsage);
    use.peak_private_mb = toMiB(counters.PeakPagefileUsage);
  }
}

double mappedPrivate() {
  std::size_t bytes = 0;
  std::uintptr_t address = 0;
  MEMORY_BASIC_INFORMATION region{};
  while (VirtualQuery(reinterpret_cast<LPCVOID>(address), &region, sizeof(region)) ==
         sizeof(region)) {
    if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE) {
      bytes += region.RegionSize;
    }
    address = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
  }
  return toMiB(bytes);
}

// Reads every adapter, since this file does not know which one the canvas's
// QRhi picked.
void readGpu(MemoryUse& use) {
  Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_IDXGIFactory1,
                                reinterpret_cast<void**>(factory.GetAddressOf())))) {
    return;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0; factory->EnumAdapters1(index, &adapter) != DXGI_ERROR_NOT_FOUND; ++index) {
    Microsoft::WRL::ComPtr<IDXGIAdapter3> queried;
    DXGI_QUERY_VIDEO_MEMORY_INFO local{};
    DXGI_QUERY_VIDEO_MEMORY_INFO shared{};
    if (FAILED(adapter.As(&queried)) ||
        FAILED(queried->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &local)) ||
        FAILED(queried->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &shared))) {
      continue;
    }
    const double local_mb = toMiB(local.CurrentUsage);
    if (!use.gpu_local_mb || local_mb > *use.gpu_local_mb) {
      use.gpu_local_mb = local_mb;
      use.gpu_shared_mb = toMiB(shared.CurrentUsage);
      use.gpu_budget_mb = toMiB(local.Budget);
    }
  }
}

#else

void readCounters(MemoryUse& use) {
  std::ifstream status("/proc/self/status");
  std::string line;
  while (std::getline(status, line)) {
    for (auto [key, mb] :
         {std::pair{"RssAnon:", &use.private_mb}, {"VmHWM:", &use.peak_private_mb}}) {
      if (line.starts_with(key)) {
        *mb = std::stod(line.substr(std::string_view(key).size())) / 1024.0;
      }
    }
  }
}

#endif

void insert(QJsonObject& json, const char* key, const std::optional<double>& mb) {
  if (mb) {
    json.insert(QLatin1StringView(key), *mb);
  }
}

}  // namespace

MemoryUse measureMemory() {
  MemoryUse use;
  readCounters(use);
  use.heap_mb = heapInUse();
#ifdef _WIN32
  use.mapped_private_mb = mappedPrivate();
  readGpu(use);
#endif
  return use;
}

QJsonObject toJson(const MemoryUse& use) {
  QJsonObject json{
      {QStringLiteral("private_mb"), use.private_mb},
      {QStringLiteral("peak_private_mb"), use.peak_private_mb},
  };
  insert(json, "mapped_private_mb", use.mapped_private_mb);
  insert(json, "heap_mb", use.heap_mb);
  insert(json, "gpu_local_mb", use.gpu_local_mb);
  insert(json, "gpu_shared_mb", use.gpu_shared_mb);
  insert(json, "gpu_budget_mb", use.gpu_budget_mb);
  return json;
}

}  // namespace pinsel

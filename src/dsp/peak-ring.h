#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

// Peaks per time bin, written by one audio thread and read by the UI without a lock. Each slot packs the bin number
// with its peak in one atomic, so a reader can tell a bin from an older one that shared the slot: gaps read as silence.
class PeakRing {
public:
	static constexpr uint64_t kBins = 1 << 14;

	PeakRing() : slots(new std::atomic<uint64_t>[kBins]())
	{
		for (uint64_t i = 0; i < kBins; i++)
			slots[i].store(0, std::memory_order_relaxed);
	}

	void put(uint64_t bin, float peak)
	{
		uint64_t q = peak >= 1.0f ? 0xFFFF : uint64_t(peak * 65535.0f);
		std::atomic<uint64_t> &slot = slots[bin & (kBins - 1)];
		uint64_t old = slot.load(std::memory_order_relaxed);
		if ((old >> 16) == tag(bin) && (old & 0xFFFF) >= q)
			return;
		slot.store((tag(bin) << 16) | q, std::memory_order_relaxed);
	}

	float get(uint64_t bin) const
	{
		uint64_t v = slots[bin & (kBins - 1)].load(std::memory_order_relaxed);
		return (v >> 16) == tag(bin) ? float(v & 0xFFFF) / 65535.0f : 0.0f;
	}

private:
	// +1: an untouched slot (all zero) must not look like bin 0.
	static uint64_t tag(uint64_t bin) { return (bin + 1) & 0xFFFFFFFFFFFFULL; }

	std::unique_ptr<std::atomic<uint64_t>[]> slots;
};

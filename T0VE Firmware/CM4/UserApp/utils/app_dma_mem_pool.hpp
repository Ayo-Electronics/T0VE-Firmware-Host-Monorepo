/*
 * app_dma_mem_pool.hpp
 *
 *  Created on: Jun 27, 2025
 *      Author: govis
 */

#pragma once

#include <app_proctypes.hpp> //for uint8_t
#include <type_traits>
#include <new>
#include <array>
#include <span>


static constexpr size_t DMA_MEM_SIZE = 16384;

class DMA_MEM_POOL {
public:
	__attribute__((section(".MEM_DMA_Section"), aligned(32)))
		static inline uint8_t pool[DMA_MEM_SIZE];
	static inline size_t offset = 0;

	template<typename T>
	static T* allocate() {
		//sanity check that this is a type we can allocate
		static_assert(std::is_trivially_constructible_v<T>, "T must be trivially constructible");
		static_assert(std::is_trivially_destructible_v<T>, "T must be trivially destructible");

		//make sure we allocate space at a 4-byte boundary for the DMA system
		constexpr size_t alignment = 4;  // force 4-byte alignment
		size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1); // round up to next multiple of 4

		if (aligned_offset + sizeof(T) > DMA_MEM_SIZE) {
			assert(false && "DMA_MEM_POOL exhausted");
			return nullptr;
		}

		T* ptr = new (&pool[aligned_offset]) T(); // default-construct in-place
		offset = aligned_offset + sizeof(T);
		return ptr;
	}

	// Allocate an array and return a std::span<T>
	template<typename T, size_t N>
	static std::span<T> allocate_buffer() {
		std::array<T, N>* array_ptr = allocate<std::array<T, N>>();
		if (!array_ptr) return {};
		return std::span<T>(array_ptr->data(), N);
	}

	static void reset() {
		offset = 0;
	}
};

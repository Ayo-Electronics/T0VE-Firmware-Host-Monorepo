/*
 * app_registers.hpp
 * Class that wraps register reads/writes/operations into a class
 * Makes pointer referencing/dereferencing a little less scary/error prone
 *
 * Trying to hella inline everything to add no overhead to any register operations
 *
 *  Created on: Jun 9, 2025
 *      Author: govis
 */

#pragma once

#include <app_proctypes.hpp>
//forcing aggressive compiler optimizations
#pragma GCC push_options
#pragma GCC optimize ("Ofast,unroll-loops,inline-functions")

template<typename t = uint32_t> //what kinda value it points to
class Register final { //prevent derivation, minimizes overhead from compiler wanting to virtualize
public:
	//========================= CONSTRUCTOR ==========================
    constexpr explicit Register(const t address) noexcept: addr(reinterpret_cast<volatile t*>(address)) {} //just save this address
    constexpr explicit Register(volatile t* address) noexcept: addr(address) {} //save the address, useful for STM32 HAL registers

    //======================= EXPLICIT READ/WRITE =======================
    __attribute__((always_inline)) inline void write(t value) noexcept { *addr = value; }

    __attribute__((always_inline)) inline t read() const noexcept { return *addr; }

    //======================== OVERRIDES ===========================
    // Assignment operator: writes value to the register
    __attribute__((always_inline)) inline void operator=(t value) noexcept { write(value); }

    // Implicit conversion to uint32_t: reads value
    __attribute__((always_inline)) inline operator t() const noexcept { return read(); }

    // Implicit conversion to uint32_t*: returns a pointer to the address
    __attribute__((always_inline)) inline operator volatile t*() const noexcept { return addr; }

    // Bitwise OR assignment
    __attribute__((always_inline)) inline void operator|=(t value) noexcept { *addr |= value;  }

    // Bitwise AND assignment
    __attribute__((always_inline)) inline void operator&=(t value) noexcept { *addr &= value;  }

    // Bitwise XOR assignment
    __attribute__((always_inline))inline void operator^=(t value) noexcept { *addr ^= value;  }

private:
    volatile t* const addr;
};

#pragma GCC pop_options

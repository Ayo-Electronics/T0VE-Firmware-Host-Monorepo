/*
 * app_string.hpp
 *
 *  Created on: Oct 8, 2025
 *      Author: govis
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"

template<size_t STRING_SIZE = 64, uint8_t PADDING = 0>
class App_String {
public:
	//======================== CONSTRUCTORS =======================
	//default constructor
	constexpr App_String() : string_data{}, actual_length(0)
	{
		//just fill with padding
		string_data.fill(PADDING);
	}

	//constructor from `std::array`
	template<size_t N>
	constexpr App_String(const std::array<uint8_t, N>& arr)
	    : string_data{}, actual_length(N)
	{
	    //sanity check that string sizes match
	    static_assert(N <= STRING_SIZE, "array initializer too large for string wrapper");

	    // Fill with padding
	    //and drop the rest of the characters to the beginning of the array
	    string_data.fill(PADDING);
	    for (size_t i = 0; i < N; i++) string_data[i] = arr[i];
	}

	//initialize with a char array
	template<size_t N>
	constexpr App_String(const char (&init)[N]) : string_data{}, actual_length(N - 1)
	{
		//sanity check sizes match
		static_assert((N - 1) <= STRING_SIZE, "array initializer too large for string wrapper");

		//pad all characters with padding
		//and drop the rest of the characters to the beginning of the array
		string_data.fill(PADDING);
		for (size_t i = 0; i < N - 1; i++) string_data[i] = init[i];
	}

	// Constructor from std::string
	constexpr App_String(const std::string& init) : string_data{}, actual_length(min(init.size(), STRING_SIZE))
	{
		// pad all characters with padding
		//and copy as much fits
		string_data.fill(PADDING);
		for (size_t i = 0; i < actual_length; i++) string_data[i] = init[i];
	}

	//copy constructor
	template<size_t OTHER_SIZE, uint8_t OTHER_PADDING>
	constexpr App_String(const App_String<OTHER_SIZE, OTHER_PADDING>& other)
		//zero init our string, and set our string size to the smaller of the string sizes
		: string_data{}, actual_length(std::min(STRING_SIZE, other.actual_length))
	{
		//now pad our string and fill with the other string's data
		string_data.fill(PADDING);
		for (size_t i = 0; i < actual_length; i++)
			string_data[i] = other.string_data[i];
	}

	//============================= ASSIGNMENT OPERATORS ===============================

	//assignment operator is basically the copy constructor
	template<size_t OTHER_SIZE, uint8_t OTHER_PADDING>
	void operator=(const App_String<OTHER_SIZE, OTHER_PADDING>& other) {
		//can't static assert our size so just copy as much as we can
		//start by padding our local string
		string_data.fill(PADDING);

		//copy as much as we can
		size_t copy_length = min(STRING_SIZE, other.actual_length);
		std::copy(other.string_data.begin(), other.string_data.begin() + copy_length, string_data.begin());

		//and set our length to the size of the other string (or as much as we could fit)
		actual_length = copy_length;
	}

	// Assignment operator from std::string
	void operator=(const std::string& init) {
		// pad all characters with padding
		string_data.fill(PADDING);
		// copy as much as fits
		size_t copy_length = std::min(init.size(), STRING_SIZE);
		for (size_t i = 0; i < copy_length; i++)
			string_data[i] = init[i];
		actual_length = copy_length;
	}

	//and an assignent operator via a character array is basically the char array initializer
	template<size_t N>
	void operator=(const char init[N]) {
		//sanity check input size to see if we can fit (ignore null terminator)
		static_assert((N-1) <= STRING_SIZE, "array initializer too large for string wrapper");

		//pad and copy - DON'T INCLUDE NULL TERMINATION
		string_data.fill(PADDING);
		std::copy(&init[0], &init[N-2], string_data.begin());

		//and save our actual length - no null termination
		actual_length = N-1;
	}

	//assignment operator from `std::array` is basically the constructor
	template<size_t N>
	void operator=(const std::array<uint8_t, N>& arr) {
	    //sanity check input size
		static_assert(N <= STRING_SIZE, "array initializer too large for string wrapper");

	    // Reset and fill
	    string_data.fill(PADDING);
	    for (size_t i = 0; i < N; i++) string_data[i] = arr[i];

	    //and save our actual length
	    actual_length = N;
	}

	//====================== UTILITY CONVERSIONS =======================
	//short function handles to cast into friendly utility types

	// Make both const and non-const overloads for span() (just views valid region of string)
	std::span<uint8_t, std::dynamic_extent> span() {
		return std::span<uint8_t, std::dynamic_extent>(string_data.data(), actual_length);
	}
	std::span<const uint8_t, std::dynamic_extent> span() const {
		return std::span<const uint8_t, std::dynamic_extent>(string_data.data(), actual_length);
	}

	//and get an uneditable reference to the
	const std::array<uint8_t, STRING_SIZE>& array() const { return string_data; }

	//======================= EQUALITY COMPARISON OPERATOR ======================
	template<size_t OTHER_SIZE, uint8_t OTHER_PADDING>
	constexpr bool operator==(const App_String<OTHER_SIZE, OTHER_PADDING>& other) const {
	    //check to see if the lengths match
		if (actual_length != other.size()) return false;

		//then just compare the underlying memory to see if its equal
	    return memcmp(string_data.data(), other.string_data.data(), actual_length) == 0;
	}

	//======================= MUTATORS ======================

	//concatenate with bytes
	void cat(const std::span<const uint8_t, std::dynamic_extent> bytes) {
		//best effort concatenation, truncate if we can't fit
		auto to_copy = min(STRING_SIZE - actual_length, bytes.size());
		if(to_copy != bytes.size()); // Debug::WARN("App String concatenation truncated!"); //debug requires app string

		//grow the string by the correct number of bytes
		std::copy(bytes.begin(), bytes.begin() + to_copy, string_data.begin() + actual_length);
		actual_length += to_copy;
	}

	//concatenate with char array
	template<size_t N>
	void cat(const char (&other)[N]) {
		//make a span with the char array and cat using span constructor
		std::span<const uint8_t, N> char_span(reinterpret_cast<const uint8_t*>(other), N);
		cat(char_span);
	}

	//concatenate with other app string
	template<size_t OTHER_SIZE, uint8_t OTHER_PADDING>
	void cat(const App_String<OTHER_SIZE, OTHER_PADDING>& other) {
		//use span concatenation
		cat(other.span());
	}

	//======================= ACCESSORS =========================
	constexpr size_t size() const { return actual_length; }

	constexpr char operator[](size_t idx) const {
		return static_cast<char>(string_data[idx]);
	}


private:
	//store the string as an array
	std::array<uint8_t, STRING_SIZE> string_data;
	size_t actual_length;
};

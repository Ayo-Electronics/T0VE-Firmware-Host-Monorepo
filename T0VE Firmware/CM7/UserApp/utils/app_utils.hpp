#pragma once

#include <span>
#include <array>
#include <string>
#include <cmath>
#include <cstring> //for memcpy
#include <algorithm> //for std::transform
#include <type_traits> //for callback function macros

#include "app_types.hpp"

//===================== NUMERICAL CONSTANTS =====================

//no real convenient way to set up pi, so just defining a literal
constexpr float PI = 3.14159265358979323846;
constexpr float TWO_PI = 2*PI;
constexpr float CPU_FREQ_HZ = 480e6; //480MHz CPU clock frequency, used for timing calculations

//============================ ARDUINO-STYLE MAP FUNCTION =========================

template<typename T>
T clip(T input, T in_min, T in_max) {
	if(input < in_min) return in_min;
	if(input > in_max) return in_max;
	return input;
}

template<typename T>
T map(T in_min, T in_max, T out_min, T out_max, T input) {
	input = clip<T>(input);
	return map_unbounded<T>(in_min, in_max, out_min, out_max, input);
}

template<typename T>
T map_unbounded(T in_min, T in_max, T out_min, T out_max, T input) {
	return out_min + (out_max - out_min)/(in_max - in_min) * (input-in_min);
}

//=========================== CALLBACK FUNCTION HELPERS ========================

/*
 * Callback function "typedefs" essentially
 * I'll start with this: IT'S REEEEEEEAAAAALLLYYYYY DIFFICULT 'CLEANLY' CALL `void()` MEMBER FUNCTIONS OF CLASSES
 * WHILE ALSO MAINTAINING LOW OVERHEAD (i.e. AVOIDING STD::FUNCTION)
 * 	\--> Re: this latter point, people claim that std::function requires heap usage and a lotta extra bloat
 *
 * `Callback Function` supports:
 * 		- attachment of a global, "c-style" function
 * 		- attachment of a static member function of a class
 * 		- attachment of non-capturing lambda functions
 * 		- attachment of a member function of a class instance [NOTE THIS IS NOT PERFORMED SUPER SAFELY, NECESSARY EVIL FOR TYPE ERASURE, SEE BELOW]
 * 		- Default constructor is "safe" i.e. calling an uninitialized `Callback Function` will do nothing rather than seg fault
 * 		- call the callback using the standard `()` operator syntax
 * 		- no heap usage, no need for persistent storage (other than the functions themselves)
 * 			- copy, move, destruction all fair game
 *
 *
 * Insipred by this response in a PJRC forum:
 * https://forum.pjrc.com/threads/70986-Lightweight-C-callbacks?p=311948&viewfull=1#post311948
 * and this talk (specifically at this timestamp):
 * https://youtu.be/hbx0WCc5_-w?t=412
 *
 * Aside: Why haven't I included support for capturing lambda functions?
 * In a sentence: It's because persistent storage of lambda closure information gets tricky.
 * In more detail: When we pass around a lambda, we are essentially passing around a class/struct that
 * most of the time, gets temporarily created on the stack (you have to be very deliberate for a heap initialized/static lambda function).
 * As far as I understand, this class/struct that contains all the lambda information is called a /lambda closure/. Calling a lambda function
 * is basically a matter of invoking an `operator()` override function on the struct, using stored variables in the closure as necessary during
 * said function call. Doing this is absolutely fine when the lambda function is in scope, and why the atomic variable's `with()` function
 * can accept a capturing lambda without any issue. Our callback function is a bit different, however. If we create a lambda function on the stack
 * like we normally do, *it will go out of scope at the end of the function call*. As such, we cannot simply store a reference to the lambda
 * closure! If we did and we tried to call the lambda function, the closure would likely contain garbage since it's not in scope anymore!
 * If we wanted to support capturing lambdas, we'd have to *allocate and manage persistent storage for the lambda closure*. This gets tricky really fast
 * especially when we need to create/destroy these functions on the fly. It's basically the bane of embedded programmers--heap usage!
 * In fact, if we want to support capturing lambdas, it starts making sense to just use std::function which has all of the safety stuff built in.
 * Doing this, however, incurs a pretty big performance and safety penalty, however. I don't want to open pandora's box with this relatively
 * low-return quality-of-life feature, so I'm avoiding implementing this for now.
 * Getting this in here now too: `std::function_ref` (slated to be introduced in C++26) is NOT a valid solution to this problem--it is basically
 * what we have here--a lightweight callback wrapper for IN-SCOPE functions--calling this on a no-longer valid function handle results in undefined behavior!
 */

//aliases to make code a bit more readable
using Instance_Storage_t = void*; //allow the instance to have its members modified in downstream calls
template<typename R = void>
using Function_t = R(*)(); //redirect to a function returning R
template<typename R = void>
using Forward_Function_t = R(*)(Instance_Storage_t); //Callbacks will store a forward function

template<typename R = void>
class Callback_Function {
public:
	//============== EMPTY CALLBACKS FOR DEFAULT INITIALIZATION ==============
	static constexpr R empty_func() { return R(); } //upon default initialization, just point to this empty function; returns default initialized return type
	static constexpr R empty_ffunc(Instance_Storage_t) { return R(); } //point the empty forwarding function to this; returns default initialized return type

	//=============== CONSTRUCTORS =============
	constexpr Callback_Function()
		: instance(nullptr), func(empty_func), forward_func(empty_ffunc) {} //default initializer

	Callback_Function(Function_t<R> _func)
		: instance(nullptr), func(_func), forward_func(empty_ffunc) {} //use this for global/static/non-capturing lambda execution

	template <typename F, typename = std::enable_if_t<std::is_convertible_v<F, Function_t<R>>>>
	Callback_Function(F f)
		: instance(nullptr), func(static_cast<Function_t<R>>(f)), forward_func(empty_ffunc) {} //for non-capturing lambdas

	Callback_Function(Instance_Storage_t _instance, Forward_Function_t<R> _forward_func)
		: instance(_instance), func(empty_func), forward_func(_forward_func) {} //for passing an instance function

	//=============== CALL OPERATOR =============
	R __attribute__((optimize("O3"))) operator()() const {
		if(instance == nullptr) return func();
		else return forward_func(instance);
	}

private:
	Instance_Storage_t instance; //holds a pointer to the instance we'd like to redirect to
	Function_t<R> func; //call this if we're executing a global function or lambda with empty capture
	Forward_Function_t<R> forward_func; //call this when we're executing on an instance
};

template <typename T, typename R, R(T::*instance_func)()>
class Instance_Callback_Function {
public:
	//========== FORWARDING FUNCTION ============
	static inline constexpr R forward_func_template(Instance_Storage_t _instance) {
		auto instance = reinterpret_cast<T*>(_instance); //this is safe
		return (instance->*instance_func)(); //this syntax was recommended by ChatGPT
	}

	//========= CONSTRUCTORS ===========
	Instance_Callback_Function(): instance(nullptr) {} //default constructor, should never be used
	Instance_Callback_Function(T* _instance): instance(_instance) {} //constructor that takes an actual instance

	//========= CAST OPERATOR OVERRIDE =========
	explicit operator Callback_Function<R>() const {
		if(instance)
			return Callback_Function<R>(reinterpret_cast<Instance_Storage_t>(instance), forward_func_template);
		else
			return Callback_Function<R>();
	}

private:
	T* instance; //holds a pointer to the instance we'd like to redirect to
};

//###### macro functions to make callback binding easier #########
// These macros now deduce the return type R from the member function pointer type
#define MAKE_CALLBACK(instance, method) \
    Instance_Callback_Function< \
        std::remove_pointer_t<decltype(instance)>, \
        std::invoke_result_t<decltype(&std::remove_pointer_t<decltype(instance)>::method), std::remove_pointer_t<decltype(instance)>*>, \
        &std::remove_pointer_t<decltype(instance)>::method \
    >(instance)

#define BIND_CALLBACK(instance, method) \
    static_cast<Callback_Function< \
        std::invoke_result_t<decltype(&std::remove_pointer_t<decltype(instance)>::method), std::remove_pointer_t<decltype(instance)>* > \
    >>(MAKE_CALLBACK(instance, method))

//============================================== ARRAY TO SPAN SLICING AND CONVERSION UTILTIES ===============================================

/*
 * Functions that take a std::array along with slice indices [begin, end)
 * and return a span that refers to the section of the array.
 * ASSUMES INDICES ARE VALID - DOING SO FOR PERFORMANCE REASONS
 *
 * These functions now accept begin and end as runtime parameters.
 * The returned span will have dynamic extent, as fixed-extent spans require compile-time constants.
 */

// Non-const overloads
template <typename T, size_t len>
inline std::span<T> section(std::array<T, len>& arr, size_t begin, size_t end) {
    // User is responsible for ensuring begin <= end && end <= len
    return std::span<T>(arr.data() + begin, end - begin);
}

template <typename T, size_t len>
inline std::span<T> trim_end(std::array<T, len>& arr, size_t end) {
    // User is responsible for ensuring end <= len
    return std::span<T>(arr.data(), end);
}

template <typename T, size_t len>
inline std::span<T> trim_beg(std::array<T, len>& arr, size_t start) {
    // User is responsible for ensuring start <= len
    return std::span<T>(arr.data() + start, len - start);
}

// Const overloads
template <typename T, size_t len>
inline std::span<const T> section(const std::array<T, len>& arr, size_t begin, size_t end) {
    // User is responsible for ensuring begin <= end && end <= len
    return std::span<const T>(arr.data() + begin, end - begin);
}

template <typename T, size_t len>
inline std::span<const T> trim_end(const std::array<T, len>& arr, size_t end) {
    // User is responsible for ensuring end <= len
    return std::span<const T>(arr.data(), end);
}

template <typename T, size_t len>
inline std::span<const T> trim_beg(const std::array<T, len>& arr, size_t start) {
    // User is responsible for ensuring start <= len
    return std::span<const T>(arr.data() + start, len - start);
}


//============================================================================================================================================

/*
 * #### BYTE PACKING AND UNPACKING UTILITIES ####
 *
 * I've been hemming and hawing about what's the most robust, c++ style way to convert between primitive data types and their underlying bit representations
 * And haven't been getting too many satisfying answers. A lot of the techniques for doing so may be affected by the endianness of the underlying machine.
 * So far, I've come across `reinterpret_cast`, using unions, and memcpy.
 *
 * I'm gonna try the `reinterpret_cast` technique due to its performance and canonically c++ syntax, but will revisit this implementation if promises arise
 * NOTE: apparently the modern C++20 way to do this is to use `bit_cast<>`--this should guarantee defined behavior across compilers with reasonable performance
 * However, STM32CubeIDE doesn't support C++20 just yet--will table this change for the future [TODO]
 *
 * Additionally, I'm gonna avoid the use of std::strings in the code generally and encode everything as a character array
 * This should jive a little better with fixed-sized data structures and general memory safety of encoding/packing operations done throughout code
 *
 * NOTE: THESE METHODS ASSUME THAT `buf` HAS VALID ARRAY INDICES [0], [1], [2], [3] (or [`n`] for the string packing function)
 * NO BOUNDS CHECKING IS EXPLICITLY DONE FOR PERFORMANCE REASONS
 */

void pack(const uint32_t val, std::span<uint8_t, std::dynamic_extent> buf); //pack a uint32_t value in big endian order
void pack(const int32_t val, std::span<uint8_t, std::dynamic_extent> buf); //pack an int32_t value in big endian order
void pack(const float val, std::span<uint8_t, std::dynamic_extent> buf); //pack an IEEE 754 single-precision float in big endian order
void pack(const std::string& text, std::span<uint8_t, std::dynamic_extent> buf); //pack an ASCII string into bytes excluding any NULL termination

uint32_t unpack_uint32(std::span<const uint8_t, std::dynamic_extent> buf); //unpack a uint32_t value in big endian order
int32_t unpack_int32(std::span<const uint8_t, std::dynamic_extent> buf); //unpack an int32_t value in big endian order
float unpack_float(std::span<const uint8_t, std::dynamic_extent> buf); //unpack an IEEE 754 single-precision float in big endian order

//============================================================================================================================================
 /*
  * String literal container initializer
  * Useful for initializing std::arrays to hold string values to have a consistent way of storing aggregate, fixed-size data in the program
  *
  * Code lifted from:
  * https://stackoverflow.com/questions/33484233/how-to-initialize-a-stdarraychar-n-with-a-string-literal-omitting-the-trail
  */

template <size_t N, size_t ... Is>
constexpr std::array<uint8_t, N - 1> s2a(const char (&a)[N], std::index_sequence<Is...>)
{
    return {{a[Is]...}};
}

template <size_t N>
constexpr std::array<uint8_t, N - 1> s2a(const char (&a)[N])
{
    return s2a(a, std::make_index_sequence<N - 1>());
}

//==========================================================================================================================================
/*
 * Float string formatting utility
 *
 * Simple utility to print floats with a fixed precision
 * since float formatting can be a bit tricky/memory intensive through library techniques
 * and sometimes requires compiler/build settings to be changed affecting code portability
 *
 * I'll templatize this function for a slight degree of anticipated compiler optimization
 * however, this isn't meant to be a fast function so use in non-performance-critical scenarios
 * ASSUMING THAT THE FLOATING POINT VALUE CAN FIT INTO A SIGNED LONG TYPE (+/- 2.147 billion)
 *
 * Inspired by:
 *  - https://stackoverflow.com/questions/47837838/get-decimal-part-of-a-number
 *  - https://stackoverflow.com/questions/28334435/stm32-printf-float-variable
 *  - https://stackoverflow.com/questions/6143824/add-leading-zeroes-to-string-without-sprintf
 */

template<size_t precision> //how many decimal points to print
inline std::string f2s(float val) {
	constexpr float scaling = std::pow(10.0, (float)precision);

	//convert to exclusively positive numbers, prepend a "-" as necessary
	std::string sign_string = val < 0 ? "-" : "";
	val = std::abs(val);

	float integer_part; //modf requires a float argument for the integer part
	float decimal = std::modf(val, &integer_part); //separate the float into its decimal and integer part

	//format the decimal part of the string first by calculating precision, then zero padding appropriately
	std::string decimal_string = std::to_string((long)std::round(decimal * scaling));
	std::string padded_decimal_string = std::string(precision - std::min(precision, decimal_string.length()), '0') + decimal_string;

	//concatenate integer and decimal parts of the string and return
	return sign_string + std::to_string((long)integer_part) + "." + padded_decimal_string;
}

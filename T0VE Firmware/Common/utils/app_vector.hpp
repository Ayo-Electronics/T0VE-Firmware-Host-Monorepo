/*
 * app_vector.hpp
 *
 *  Created on: Oct 16, 2025
 *      Author: govis
 *
 * Idea is to replicate some functionality of a std::vector while maintaining fixed-size allocation
 * Size and type is configurable via template parameters
 * Underlying container is a std::array
 */

#pragma once

#include "app_proctypes.hpp"
#include "app_utils.hpp"
#include "app_debug_if.hpp"

template<typename T, size_t MAX_N>
class App_Vector {

	static_assert(std::is_move_constructible_v<T>, "App_Vector<T> requires move-constructible T");

public:
	//========================== CONSTRUCTORS/DESTRUCTORS ============================
	// No elements constructed initially
	constexpr App_Vector() noexcept : last_elem(0) {}

	//C array constructor
	template<size_t N>
	constexpr App_Vector(const T (&init)[N]) : last_elem(0) {
		//sanity check array size
		static_assert(N <= MAX_N, "initializer too large");

		//use the object copy constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		for (size_t i = 0; i < N; i++) std::construct_at(ptr(i), init[i]);
		last_elem = N;
	}

	//std::array constructor
	template<size_t N>
	constexpr App_Vector(const std::array<T, N>& init) : last_elem(0) {
		//sanity check array size
		static_assert(N <= MAX_N, "initializer too large");

		//use the object copy constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		for (size_t i = 0; i < N; i++) std::construct_at(ptr(i), init[i]);
		last_elem = N;
	}

	//span constructor, constant size
	template<size_t N>
	constexpr App_Vector(std::span<const T, N> init) : last_elem(0) {
		//sanity check array size
		static_assert(N <= MAX_N, "initializer too large");

		//use the object copy constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		for (size_t i = 0; i < N; i++) std::construct_at(ptr(i), init[i]);
		last_elem = N;
	}

	//span constructor, dynamic size
	//TODO if necessary; a bit more dangerous

	//explicit move constructor--move objects from old container into new container
	template<size_t N>
	App_Vector(App_Vector<T, N>&& other) : last_elem(0) {
		//sanity check array size
		static_assert(N <= MAX_N, "initializer too large");

		//use the object's move constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		for (size_t i = 0; i < other.size(); i++) std::construct_at(ptr(i), std::move(other[i]));
		last_elem = other.size();

		//and for extra safety, clear the old container
		other.clear();
	}

	//explicit destructor to call object destructors
	~App_Vector() { clear(); }

	//========================= COPY/MOVE/ASSIGNMENT =========================

	//copy constructor
	template<size_t N>
	App_Vector(const App_Vector<T, N>& other) : last_elem(0) {
		//sanity check size
		//guaranteed safe behavior even if other vector isn't completely full
		static_assert(N <= MAX_N, "initializer too large");

		//use the object copy constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		//also initialize the size appropriately
		for (size_t i = 0; i < other.size(); i++) std::construct_at(ptr(i), other[i]);

		//finally, update the size accordingly
		last_elem = other.size();
	}

	//assignment operator to arbitrary sized vector, mirrors copy constructor
	template<size_t N>
	void operator=(const App_Vector<T, N>& other) {
		//sanity check size
		//guaranteed safe behavior even if other vector isn't completely full
		static_assert(N <= MAX_N, "initializer too large");

		//don't assign from itself, only valid if they're the same size
		if constexpr (N == MAX_N) {
			if (this == &other) return;
		}

		//destroy all old objects explicitly
		for (size_t i = 0; i < last_elem; i++) std::destroy_at(ptr(i));

		//use the object copy constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		//also initialize the size appropriately
		for (size_t i = 0; i < other.size(); i++) std::construct_at(ptr(i), other[i]);

		//finally, update the size accordingly
		last_elem = other.size();
	}

	//move assignment operator, elaborates on move constructor
	template<size_t N>
	void operator=(App_Vector<T, N>&& other) {
		//sanity check size
		//guaranteed safe behavior even if other vector isn't completely full
		static_assert(N <= MAX_N, "initializer too large");

		//don't assign from itself, only valid if they're the same size
		if constexpr (N == MAX_N) {
			if (this == &other) return;
		}

		//destroy all old objects explicitly
		for (size_t i = 0; i < last_elem; i++) std::destroy_at(ptr(i));

		//use the object's move constructor to initialize the element at the particular index
		//this syntax means we don't default construct instances we haven't yet formally created
		for (size_t i = 0; i < other.size(); i++) std::construct_at(ptr(i), std::move(other[i]));

		//finally, update the size accordingly
		last_elem = other.size();

		//for extra safety, clear the other container
		other.clear();
	}

	//==================== CAPACITY/SIZE ======================
	constexpr size_t capacity() const { return MAX_N; }
	size_t size() const { return last_elem; }
	bool empty() const { return last_elem == 0; }

	//========================== ELEMENT ACCESS ==========================
	T& operator[](size_t i) { return *ptr(i); }				//element access
	const T& operator[](size_t i) const { return *ptr(i); }	//const overload of element access

	T& front() noexcept { return *ptr(0); }					//access first element (UB if empty)
	const T& front() const noexcept { return *ptr(0); }		//const overload of previous function

	T& back() noexcept { return *ptr(last_elem - 1); }				//access last element (UB if empty)
	const T& back() const noexcept { return *ptr(last_elem - 1); }	//const overload of previous function

	T* data() { return ptr(0); }							//pointer to first element of storage
	const T* data() const { return ptr(0); }				//const overload of previous function

	//=========================== ITERATORS/SPAN ==============================
	//useful for copies and range-based for loops
	T* begin() { return data(); }
	T* end() { return data() + last_elem; }
	const T* begin() const { return data(); }
	const T* end() const { return data() + last_elem; }
	std::span<T> span() { return std::span<T, std::dynamic_extent>(data(), last_elem); }
	std::span<const T> span() const { return std::span<const T, std::dynamic_extent>(data(), last_elem); }
	operator std::span<T>() { return span(); }
	operator std::span<const T>() const { return span(); }

	//============================ PUSH BACK/EMPLACE BACK ===============================
	//add new element at end
	void push_back(const T& value) {
		//sanity check size, fail gracefully without crashing
		if (last_elem >= MAX_N) { Debug::ERROR("push_back [single element] overflow"); return; }

		//use the copy constructor to construct the element
		//and increment the element counter
		std::construct_at(ptr(last_elem), value);
		last_elem++;
	}


	//add span of elements at the end
	void push_n_back(const std::span<const T, std::dynamic_extent> new_elems) {
		//sanity check size, fail gracefully without crashing
		if((last_elem + new_elems.size() - 1) >= MAX_N) { Debug::ERROR("push_back [span] overflow"); return; }

		//use the copy constructor to construct the elements
		//and update the element counter accordingly
		for(size_t i = 0; i < new_elems.size(); i++) {
			std::construct_at(ptr(last_elem), new_elems[i]);
			last_elem++;
		}
	}

	//add array of elements at the end
	template<size_t N>
	void push_n_back(const std::array<T, N>& new_elems) {
		push_n_back(std::span<const T, std::dynamic_extent>(new_elems));	//use span overload
	}

	//add vector of elements at the end
	template<size_t N>
	void push_n_back(const App_Vector<T, N>& new_elems) {
		push_n_back(new_elems.span());	//use span overload
	}

	//add new element at end using move semantics
	void push_back(T&& value) {
		//sanity check size, fail gracefully without crashing
		if (last_elem >= MAX_N) { Debug::ERROR("push_back overflow"); return; }

		//use the move constructor to construct the element
		//and increment the element counter
		std::construct_at(ptr(last_elem), std::move(value));
		last_elem++;
	}

	//directly build the object using provided arguments at end
	template<typename... Args>
	void emplace_back(Args&&... args) {
		//sanity check size, fail gracefully without crashing
		if (last_elem >= MAX_N) { Debug::ERROR("emplace_back overflow"); return;}

		//directly call the non-default object constructor when creating a new object at the back
		//and increment the element counter
		std::construct_at(ptr(last_elem), std::forward<Args>(args)...);
		last_elem++;
	}

	//========================== INSERT/EMPLACE_AT =======================
	//an expensive operation, but support it
	//insert an element at the specified index, shifting everything right after the index; copies object
	void insert(size_t pos, const T& value) {
		//sanity check sizes
		if (pos > last_elem || last_elem >= MAX_N) { Debug::ERROR("insert out/overflow"); return; }

		//have to do this non-trivial shifting operation; moves objects over if possible, otherwise copies
		//and explicitly destroys objects as they get shifted
		shift_right_from(pos);

		//use the copy constructor to construct the element
		//and increment the element counter
		std::construct_at(ptr(pos), value);
		last_elem++;
	}

	//an expensive operation, but support it
	//insert an element at the specified index, shifting everything right after the index; directly creates object
	template<typename... Args>
	void emplace(size_t pos, Args&&... args) {
		//sanity check sizes
		if (pos > last_elem || last_elem >= MAX_N) { Debug::ERROR("emplace out/overflow"); return; }

		//have to do this non-trivial shifting operation; moves objects over if possible, otherwise copies
		//and explicitly destroys objects as they get shifted
		shift_right_from(pos);

		//directly call the non-default object constructor when creating a new object at the back
		//and increment the element counter
		std::construct_at(ptr(pos), std::forward<Args>(args)...);
		last_elem++;
	}

	//============================= ERASE/POP/CLEAR ==============================
	//erase an element at the specified position, shifting items left (nontrivial)
	void erase(size_t pos) {
		//sanity check index
		if (pos >= last_elem) { Debug::ERROR("erase out of bounds"); return; }

		//explicitly call the object destructor
		std::destroy_at(ptr(pos));

		//have to do this non-trivial shifting operation; moves object over if possible, otherwise copies
		//and explicitly destroys objects as they get shifted
		shift_left_from(pos);
		last_elem--;
	}

	//NOT SUPPORTING pop_back - hard to make this resilient with non-default constructible T
//	T pop_back() noexcept {
//		if (last_elem == 0) { Debug::ERROR("pop_back empty"); return T{}; }
//		--last_elem;
//		T tmp = std::move(*ptr(last_elem));
//		std::destroy_at(ptr(last_elem));
//		return tmp;
//	}

	//destruct all objects
	void clear() noexcept {
		for (size_t i = 0; i < last_elem; i++) std::destroy_at(ptr(i));
		last_elem = 0;
	}

private:
	//instead of an array of objects, just have an array of space to store the objects
	//means we don't have to construct all objects at instantiation
	using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;
	Storage storage[MAX_N];
	size_t last_elem;

	//note: this code looks really hokey, but apparently necessary since `storage` isn't an array of objects
	//		but rather an array of bytes big enough to store objects in an aligned way (motivates the `reinterpret_cast<>()`
	//		the `std::launder` is apparently necessary since we're potentially doing some on-the-fly construction/destruction of objects
	//		and this tells the compiler to "expect something normal" or something along those lines
	T* ptr(size_t i) { return std::launder(reinterpret_cast<T*>(&storage[i])); }
	const T* ptr(size_t i) const { return std::launder(reinterpret_cast<const T*>(&storage[i])); }

	//shift objects to the right using move constructor
	//making sure to explicitly destroy objects after they're moved
	void shift_right_from(size_t from) {
		for (size_t i = last_elem; i > from; --i) {
			std::construct_at(ptr(i), std::move(*ptr(i - 1)));
			std::destroy_at(ptr(i - 1));
		}
	}

	//shift objects to the left using move constructor
	//making sure to explicitly destroy objects after they're moved
	void shift_left_from(size_t from) {
		for (size_t i = from; i + 1 < last_elem; i++) {
			std::construct_at(ptr(i), std::move(*ptr(i + 1)));
			std::destroy_at(ptr(i + 1));
		}
	}
};

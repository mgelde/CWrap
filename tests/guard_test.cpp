/*   Copyright 2016-2019 Marcus Gelderie
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 *
 */

#include <memory>

#include "gtest/gtest.h"

#include "cppc.hpp"

#include "test_api.h"

using namespace ::cppc;
using namespace ::cppc::testing::mock;
using namespace ::cppc::testing::mock::api;
using namespace ::cppc::testing::assertions;

template <class T = DefaultFreePolicy<some_type_t>, class S = ByValueStoragePolicy<some_type_t>>
using GuardT = Guard<some_type_t, T, S>;

template <class T>
struct CustomDeleter {
    static unsigned int numberOfConstructorCalls;

    CustomDeleter() { numberOfConstructorCalls++; }

    CustomDeleter(const CustomDeleter &) { numberOfConstructorCalls++; }

    CustomDeleter(CustomDeleter &&) = default;

    CustomDeleter &operator=(const CustomDeleter &&) { numberOfConstructorCalls++; }

    CustomDeleter &operator=(CustomDeleter &&) = default;

    void operator()(T &t) const noexcept { release_resources(&t); }
};

template <class T>
unsigned int CustomDeleter<T>::numberOfConstructorCalls{0};

using CustomDeleterT = CustomDeleter<some_type_t>;

class GuardFreeFuncTest : public ::testing::Test {
public:
    void SetUp() override { MockAPI::instance().reset(); }
};

TEST_F(GuardFreeFuncTest, testFunctionPointerAsFreeFunc) {
    auto guard = new Guard<some_type_t *, void (*)(some_type_t *)>{&free_resources,
                                                                   create_and_initialize()};
    ASSERT_NOT_CALLED(MockAPI::instance().freeResourcesFunc());
    delete guard;
    ASSERT_CALLED(MockAPI::instance().freeResourcesFunc());
}

TEST_F(GuardFreeFuncTest, testPassingPointerToAllocatedMemory) {
    {
        auto freeFunc = [](some_type_t &ptr) { release_resources(&ptr); };
        GuardT<> guard{std::move(freeFunc)};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        do_init_work(&guard.get());
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
    }
    ASSERT_CALLED(MockAPI::instance().releaseResourcesFunc());
}

TEST_F(GuardFreeFuncTest, testInitFunctionReturningPointer) {
    {
        auto freeFunc = [](some_type_t *ptr) { free_resources(ptr); };
        auto guard = Guard<some_type_t *>{freeFunc, create_and_initialize()};
        ASSERT_NOT_CALLED(MockAPI::instance().freeResourcesFunc());
    }
    ASSERT_CALLED(MockAPI::instance().freeResourcesFunc());
}

TEST_F(GuardFreeFuncTest, testWithCustomDeleter) {
    {
        GuardT<CustomDeleterT> guard{};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        do_init_work(&guard.get());
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
    }
    ASSERT_CALLED(MockAPI::instance().releaseResourcesFunc());
}

TEST_F(GuardFreeFuncTest, testWithUniquePointer) {
    {
        GuardT<CustomDeleterT, UniquePointerStoragePolicy<some_type_t>> guard{};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        do_init_work(&guard.get());
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        static_assert(std::is_const<std::remove_reference_t<decltype(
                              const_cast<const decltype(guard) &>(guard).get())>>::value,
                      "Should return a const ref");
    }
    ASSERT_CALLED(MockAPI::instance().releaseResourcesFunc());
}

/**
 * DefaultFreePolicy is essentially a std::function<void(T&)> (with some
 * different type in case of pointers). This type is default-constructible, but
 * is empty if so constructed. If invoked when empty, it throws a
 * std::bad_function_call. This is consistent with what we want from a guard
 * that does not know how to free its guarded resource. It should not silently
 * leak resources. Instead, it should crash the program. If the client want
 * this behavior (although I wouldn't know why), a custom FreePolicy is easily
 * implemented.  This test documents that behavior.
 */
TEST_F(GuardFreeFuncTest, testThatDefaulFreePolicyThrowsIfEmpty) {
    // a guard with the CustomFreePolicy and without a function to release resources
    GuardT<> *guard = new GuardT<>();

    // make sure the destructor is called by deleting the guard
    ASSERT_THROW(delete guard, std::bad_function_call);
}

class GuardMemoryMngmtTest : public ::testing::Test {
public:
    void SetUp() override {
        CustomDeleterT::numberOfConstructorCalls = 0;
        MockAPI::instance().reset();
    }
};

/**
 * Make sure that we do not copy the object that is to be guarded in
 * needlessly.
 */
TEST_F(GuardMemoryMngmtTest, testGuardedValueIsPassedByReferece) {
    ASSERT_EQ(some_type_t::getNumberOfConstructorCalls(), 0);
    some_type_t someObject{};
    ASSERT_EQ(some_type_t::getNumberOfConstructorCalls(), 1);
    GuardT<CustomDeleterT> guard{someObject};
    // now we expect an additional call for the copy-construction of the
    // value stored inside the Guard
    ASSERT_EQ(some_type_t::getNumberOfConstructorCalls(), 2);

    // let's make sure the same overhead is incurred by the other overloads...
    CustomDeleterT freeFunc{};
    //...with copy-constructing the deleter
    GuardT<CustomDeleterT> anotherGuard{freeFunc, someObject};
    ASSERT_EQ(some_type_t::getNumberOfConstructorCalls(), 3);
    //...with move-constructing the deleter
    GuardT<CustomDeleterT> yetAnotherGuard{std::move(freeFunc), someObject};
    ASSERT_EQ(some_type_t::getNumberOfConstructorCalls(), 4);
}

// test that the overload taking only a deleter forwards correctly
TEST_F(GuardMemoryMngmtTest, testPerfectForwarding) {
    CustomDeleterT deleter{};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<CustomDeleterT> guard{deleter};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 2);
    GuardT<CustomDeleterT> anotherGuard{std::move(deleter)};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 2);
    GuardT<CustomDeleterT> yetAntotherGuard{};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 3);
}

// test that the overload taking a guarded type forwards correctly
TEST_F(GuardMemoryMngmtTest, testPerfectForwardingWithTwoArguments) {
    CustomDeleterT deleter{};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<CustomDeleterT> guard{deleter, some_type_t{}};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 2);
    GuardT<CustomDeleterT> anotherGuard{std::move(deleter), some_type_t{}};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 2);
}

// test that the right overloads are called if the deleter is a reference-type
TEST_F(GuardMemoryMngmtTest, testDeleterAsReference) {
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 0);
    CustomDeleterT deleter{};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<CustomDeleterT &> guard{deleter};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<CustomDeleterT &> anotherGuard{deleter, some_type_t{}};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
}

TEST_F(GuardMemoryMngmtTest, testDeleterAsConstReference) {
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 0);
    const CustomDeleterT deleter{};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<const CustomDeleterT &> guard{deleter};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
    GuardT<const CustomDeleterT &> anotherGuard{deleter, some_type_t{}};
    ASSERT_EQ(CustomDeleterT::numberOfConstructorCalls, 1);
}

TEST_F(GuardMemoryMngmtTest, testMoveConstruction) {
    {
        GuardT<CustomDeleterT> guard{};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        GuardT<CustomDeleterT> another{std::move(guard)};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
    }
    ASSERT_NUM_CALLED(MockAPI::instance().releaseResourcesFunc(), 1);
}

TEST_F(GuardMemoryMngmtTest, testMoveAssignment) {
    {
        GuardT<CustomDeleterT> guard{};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        GuardT<CustomDeleterT> another{std::move(guard)};
        ASSERT_NOT_CALLED(MockAPI::instance().releaseResourcesFunc());
        guard = GuardT<CustomDeleterT>{};
    }
    ASSERT_NUM_CALLED(MockAPI::instance().releaseResourcesFunc(), 2);
}

/*
 * Static tests.
 *
 */

class TypeWithouDefaultConstructor {
    TypeWithouDefaultConstructor() = delete;
};

using NonDefaultConstructibleGuard = GuardT<TypeWithouDefaultConstructor>;

static_assert(!std::is_default_constructible<NonDefaultConstructibleGuard>::value,
              "If deleter has no default constructor, should not be "
              "default-construtible");

static_assert(!std::is_default_constructible<GuardT<CustomDeleterT &>>::value,
              "If deleter is ref-type, should not be default-construtible");

static_assert(std::is_default_constructible<Guard<some_type_t>>::value,
              "Default deleter is default constructible");

static_assert(
        std::is_const<
                std::remove_reference_t<decltype(std::declval<const Guard<int>>().get())>>::value,
        "Policy respects const correctness.");

// lambdas cannot be declared in unevaluated contexts
auto __deleterLambdaPrototype = [](some_type_t &) {};

static_assert(std::is_constructible<GuardT<std::function<void(some_type_t &)>>,
                                    decltype(__deleterLambdaPrototype)>::value,
              "Constructor taking deleter policy should obey natural conversions");

static_assert(!std::is_copy_assignable<GuardT<CustomDeleterT>>::value,
              "Copy assignment should be disabled");

static_assert(!std::is_copy_constructible<GuardT<CustomDeleterT>>::value,
              "Copy construction should be disabled");

static_assert(noexcept(std::declval<GuardT<CustomDeleterT>>().~Guard()),
              "Guard with CustomDeleter should have noexcept destructor.");

static_assert(!noexcept(std::declval<GuardT<>>().~Guard()),
              "Guard with DefaultFreePolicy should not have noexcept destructor.");

#if __cplusplus < 201703L
static_assert(
        !noexcept(std::declval<Guard<int, void(&)(int) noexcept>>().~Guard()),
       "Guard with noexcept free method should not have noexcept destructor on < C++17");
static_assert(
        !noexcept(std::declval<Guard<int, void(*)(int) noexcept>>().~Guard()),
       "Guard with noexcept free method should not have noexcept destructor on < C++17");
#else
static_assert(
        noexcept(std::declval<Guard<int, void(&)(int) noexcept>>().~Guard()),
       "Guard with noexcept free method should have noexcept destructor");
static_assert(
        noexcept(std::declval<Guard<int, void(*)(int) noexcept>>().~Guard()),
       "Guard with noexcept free method should have noexcept destructor");
#endif

static_assert(!noexcept(std::declval<GuardT<void (*)(some_type_t *)>>().~Guard()),
              "Guard with function-pointer free policy should not have noexcept destructor");

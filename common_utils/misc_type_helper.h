#include "common.h"

template<typename T>
struct is_member_function_const /*intentionally undefined*/;

template<typename R, typename C, typename... Args>
struct is_member_function_const<R(C::*)(Args...)> : std::false_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_const<R(C::*)(Args...) const> : std::true_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_const<R(C::*)(Args...) noexcept> : std::false_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_const<R(C::*)(Args...) const noexcept> : std::true_type { };

template<typename T>
struct is_member_function_noexcept /*intentionally undefined*/;

template<typename R, typename C, typename... Args>
struct is_member_function_noexcept<R(C::*)(Args...)> : std::false_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_noexcept<R(C::*)(Args...) const> : std::false_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_noexcept<R(C::*)(Args...) noexcept> : std::true_type { };

template<typename R, typename C, typename... Args>
struct is_member_function_noexcept<R(C::*)(Args...) const noexcept> : std::true_type { };

template<typename T>
struct member_function_return_type /*intentionally undefined*/;

template<typename R, typename C, typename... Args>
struct member_function_return_type<R(C::*)(Args...)> {
    using type = R;
};

template<typename R, typename C, typename... Args>
struct member_function_return_type<R(C::*)(Args...) const> {
    using type = R;
};

template<typename R, typename C, typename... Args>
struct member_function_return_type<R(C::*)(Args...) noexcept> {
    using type = R;
};

template<typename R, typename C, typename... Args>
struct member_function_return_type<R(C::*)(Args...) const noexcept> {
    using type = R;
};

template<typename T>
using member_function_return_type_t = typename member_function_return_type<T>::type;

template<typename T>
struct std_array_element_type /*intentionally undefined*/;

template<typename T, size_t N>
struct std_array_element_type<std::array<T, N>> {
    using type = T;
};

template<typename T>
using std_array_element_type_t = typename std_array_element_type<T>::type;

namespace internal
{

template<typename Tuple>
struct PointerTupleToVoidStarVectorImpl
{
    template<size_t i>
    static void run(const Tuple& t, std::vector<void*>* output)
    {
        if constexpr(i < std::tuple_size_v<Tuple>)
        {
            output->push_back(GetClassMethodOrPlainFunctionPtr(std::get<i>(t)));
            run<i + 1>(t, output);
        }
    }
};

}   // namespace internal

template<typename Tuple>
void ConvertPointerTupleToVoidStarVector(const Tuple& t, std::vector<void*>* output)
{
    output->clear();
    internal::PointerTupleToVoidStarVectorImpl<Tuple>::template run<0>(t, output);
}

#define FOLD_CONSTEXPR(...) (__builtin_constant_p(__VA_ARGS__) ? (__VA_ARGS__) : (__VA_ARGS__))

namespace internal
{

template<typename Base, typename Derived>
constexpr uint64_t offsetof_base_impl()
{
    static_assert(std::is_base_of_v<Base, Derived>);
    constexpr const Derived* d = FOLD_CONSTEXPR(reinterpret_cast<const Derived*>(0x4000));
    constexpr const Base* b = static_cast<const Base*>(d);

    constexpr const uint8_t* du = FOLD_CONSTEXPR(reinterpret_cast<const uint8_t*>(d));
    constexpr const uint8_t* bu = FOLD_CONSTEXPR(reinterpret_cast<const uint8_t*>(b));

    static_assert(bu >= du);
    return bu - du;
}

template<typename Base, typename Derived, typename Enable = void>
struct offsetof_base { };

template<typename Base, typename Derived>
struct offsetof_base<Base, Derived, std::enable_if_t<std::is_base_of_v<Base, Derived>>>
{
    static constexpr uint64_t offset = offsetof_base_impl<Base, Derived>();
};

}   // namespace internal

template<typename Base, typename Derived>
constexpr uint64_t offsetof_base_v = internal::offsetof_base<Base, Derived>::offset;

namespace internal
{

template<typename T> struct MemberObjectPointerClassImpl { };

template<typename C, typename V>
struct MemberObjectPointerClassImpl<V C::*>
{
    using class_type = C;
    using value_type = V;
};

}   // namespace internal

template<typename T>
using class_type_of_member_object_pointer_t = typename internal::MemberObjectPointerClassImpl<T>::class_type;

template<typename T>
using value_type_of_member_object_pointer_t = typename internal::MemberObjectPointerClassImpl<T>::value_type;

namespace internal
{

template<auto member_object_ptr>
constexpr uint64_t offsetof_member_impl()
{
    using T = decltype(member_object_ptr);
    static_assert(std::is_member_object_pointer_v<T>);
    using C = class_type_of_member_object_pointer_t<T>;
    using V = value_type_of_member_object_pointer_t<T>;

    constexpr C* c = FOLD_CONSTEXPR(reinterpret_cast<C*>(0x4000));
    constexpr V* v = FOLD_CONSTEXPR(&(c->*member_object_ptr));

    constexpr uint64_t cu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(c));
    constexpr uint64_t vu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(v));
    return vu - cu;
}

}   // namespace internal

template<auto member_object_ptr>
constexpr uint64_t offsetof_member_v = internal::offsetof_member_impl<member_object_ptr>();

// The C++ placement-new is not type-safe: the pointer to 'new' is taken as a void*, so it will silently corrupt memory if
// the pointer type does not match the object type (e.g. if you accidentally pass in a T** instead of T*). Fix this problem.
//
template<typename T, typename... Args>
void ALWAYS_INLINE ConstructInPlace(T* ptr, Args&&... args)
{
    static_assert(!std::is_pointer_v<T>, "You don't have to call placement-new for primitive types");
    new (static_cast<void*>(ptr)) T(std::forward<Args>(args)...);
}

namespace internal
{

template<typename... Types>
struct is_typelist_pairwise_distinct_impl
{
    template<typename First, typename Second, typename... Remaining>
    static constexpr bool check_first_against_remaining()
    {
        if constexpr(std::is_same_v<First, Second>)
        {
            return false;
        }
        else if constexpr(sizeof...(Remaining) > 0)
        {
            return check_first_against_remaining<First, Remaining...>();
        }
        else
        {
            return true;
        }
    }

    template<typename First, typename... Remaining>
    static constexpr bool check_all_pairs()
    {
        if constexpr(sizeof...(Remaining) == 0)
        {
            return true;
        }
        else
        {
            if constexpr(!check_first_against_remaining<First, Remaining...>())
            {
                return false;
            }
            else
            {
                return check_all_pairs<Remaining...>();
            }
        }
    }

    static constexpr bool compute()
    {
        if constexpr(sizeof...(Types) <= 1)
        {
            return true;
        }
        else
        {
            return check_all_pairs<Types...>();
        }
    }

    static constexpr bool value = compute();
};

}   // namespace internal

template<typename... Types>
constexpr bool is_typelist_pairwise_distinct = internal::is_typelist_pairwise_distinct_impl<Types...>::value;

template<int N, typename... Types>
using parameter_pack_nth_t = std::tuple_element_t<N, std::tuple<Types...>>;

template<size_t N, typename T>
struct arg_nth_impl;

template<size_t N, typename R, typename... Args>
struct arg_nth_impl<N, R(*)(Args...)>
{
    using type = parameter_pack_nth_t<N, Args...>;
};

template<typename T, size_t N>
using arg_nth_t = arg_nth_impl<N, T>;

#pragma once

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

    if constexpr(!std::is_array_v<V>)
    {
        constexpr C* c = FOLD_CONSTEXPR(reinterpret_cast<C*>(0x4000));
        constexpr V* v = FOLD_CONSTEXPR(&(c->*member_object_ptr));

        constexpr uint64_t cu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(c));
        constexpr uint64_t vu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(v));
        return vu - cu;
    }
    else
    {
        constexpr C* c = FOLD_CONSTEXPR(reinterpret_cast<C*>(0x4000));
        constexpr std::remove_extent_t<V>* v = FOLD_CONSTEXPR(c->*member_object_ptr);

        constexpr uint64_t cu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(c));
        constexpr uint64_t vu = FOLD_CONSTEXPR(reinterpret_cast<uint64_t>(v));
        return vu - cu;
    }
}

}   // namespace internal

template<auto member_object_ptr>
constexpr uint64_t offsetof_member_v = internal::offsetof_member_impl<member_object_ptr>();

template<auto member_object_ptr>
using typeof_member_t = value_type_of_member_object_pointer_t<decltype(member_object_ptr)>;

template<auto member_object_ptr>
using classof_member_t = class_type_of_member_object_pointer_t<decltype(member_object_ptr)>;

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

template<size_t N, typename R, typename... Args>
struct arg_nth_impl<N, NO_RETURN R(*)(Args...)>
{
    using type = parameter_pack_nth_t<N, Args...>;
};

template<typename T, size_t N>
using arg_nth_t = typename arg_nth_impl<N, T>::type;

template<typename T>
struct fn_num_args_impl;

template<typename R, typename... Args>
struct fn_num_args_impl<R(*)(Args...)>
{
    constexpr static size_t value = sizeof...(Args);
};

template<typename R, typename... Args>
struct fn_num_args_impl<NO_RETURN R(*)(Args...)>
{
    constexpr static size_t value = sizeof...(Args);
};

template<typename T>
constexpr size_t fn_num_args = fn_num_args_impl<T>::value;

template<typename T>
struct is_no_return_function : std::false_type { };

template<typename R, typename... Args>
struct is_no_return_function<NO_RETURN R(*)(Args...)> : std::true_type { };

template<typename T>
constexpr bool is_no_return_function_v = is_no_return_function<T>::value;

template<typename T>
struct num_args_in_function_impl;

template<typename R, typename... Args>
struct num_args_in_function_impl<R(*)(Args...)>
{
    static constexpr size_t value = sizeof...(Args);
};

template<typename R, typename... Args>
struct num_args_in_function_impl<NO_RETURN R(*)(Args...)>
{
    static constexpr size_t value = sizeof...(Args);
};

template<typename T>
constexpr size_t num_args_in_function = num_args_in_function_impl<T>::value;

template<typename T>
struct function_return_type_impl;

template<typename R, typename... Args>
struct function_return_type_impl<R(*)(Args...)>
{
    using type = R;
};

template<typename T>
using function_return_type_t = typename function_return_type_impl<T>::type;

template<typename U, typename T>
struct tuple_append_element_impl;

template<typename U, typename... Args>
struct tuple_append_element_impl<U, std::tuple<Args...>> {
    using type = std::tuple<Args..., U>;
};

template<typename T, typename U>
using tuple_append_element_t = typename tuple_append_element_impl<U, T>::type;

template<typename... T>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<T>()...));

namespace detail {

template<typename F, typename... T>
struct CheckFirstTypeUniqueImpl
{
    static constexpr bool value = !(false || ... || std::is_same_v<F, T>);
};

// bool b, size_t k, typename F, typename... T:
// The first k types of 'F, T...' that are yet to unique
// The remaining types are the types that have been uniqued
// b must equal FirstTypeUnique<F, T...>
//
// Under this invariant, the transfer logic should be:
// 1. If b == true, transfer to FirstTypeUnique<T..., F>, k - 1, T..., F
// 2. If b == false, transfer to FirstTypeUnique<T...>, k - 1, T...
//
template<bool b, size_t k, typename F, typename... T>
struct RemoveRedundantTypeImpl
{
    static_assert(b && k > 0);  // otherwise should have been specialized
    static constexpr bool b2 = CheckFirstTypeUniqueImpl<T..., F>::value;
    using type = typename RemoveRedundantTypeImpl<b2, k - 1, T..., F>::type;
};

template<size_t k, typename F, typename... T>
struct RemoveRedundantTypeImpl<false, k, F, T...>
{
    static_assert(k > 0);   // otherwise should have been specialized
    static constexpr bool b2 = CheckFirstTypeUniqueImpl<T...>::value;
    using type = typename RemoveRedundantTypeImpl<b2, k - 1, T...>::type;
};

template<typename F, typename... T>
struct RemoveRedundantTypeImpl<true, 0, F, T...>
{
    static_assert(is_typelist_pairwise_distinct<F, T...>);
    using type = std::tuple<F, T...>;
};

template<typename F, typename... T>
struct RemoveRedundantTypeImpl<false, 0, F, T...>
{
    static_assert(type_dependent_false<F>::value, "This case should never happen");
};

// 'RemoveRedundantTypeImpl' keeps the last occurance of each type, which can be counter-intuitive
// To fix this,  we revert the type sequence before feeding into RemoveRedundantTypeImpl, and revert it back afterwards
// so that the first occurance of each type is retained
//
template<typename... T>
struct RevertTypeSequenceImpl
{
    static_assert(sizeof...(T) == 0);   // otherwise should have been specialized
    using type = std::tuple<>;
};

template<typename F, typename... T>
struct RevertTypeSequenceImpl<F, T...>
{
    using type = tuple_append_element_t<typename RevertTypeSequenceImpl<T...>::type, F>;
};

template<typename T>
struct ReverseTupleOrderImpl;

template<typename... T>
struct ReverseTupleOrderImpl<std::tuple<T...>>
{
    using type = typename RevertTypeSequenceImpl<T...>::type;
};

template<typename T>
struct DeduplicateTupleTypeImplImpl;

template<typename... T>
struct DeduplicateTupleTypeImplImpl<std::tuple<T...>>
{
    using type = typename RemoveRedundantTypeImpl<CheckFirstTypeUniqueImpl<T...>::value, sizeof...(T), T...>::type;
};

template<typename T>
struct DeduplicateTupleTypeImpl
{
    using type = typename DeduplicateTupleTypeImplImpl<T>::type;
};

// 'RemoveRedundantTypeImpl' cannot handle the empty tuple case unfortunately, so work around it
//
template<>
struct DeduplicateTupleTypeImpl<std::tuple<>>
{
    using type = std::tuple<>;
};

}   // namespace detail

template<typename T>
using reverse_tuple_element_order_t = typename detail::ReverseTupleOrderImpl<T>::type;

// Given a tuple type T, deduplicate its element types, so that only the first occurance of each element type is kept.
// The order of the tuple is retained other than the removed elements.
//
template<typename T>
using deduplicate_tuple_element_types_t = reverse_tuple_element_order_t<typename detail::DeduplicateTupleTypeImpl<reverse_tuple_element_order_t<T>>::type>;

template<typename T>
struct deduplicate_tuple
{
    using DeduplicatedTy = deduplicate_tuple_element_types_t<T>;
    static constexpr size_t numElements = std::tuple_size_v<DeduplicatedTy>;

    template<size_t ord, typename U>
    static consteval size_t FindOrdInDeduplicatedTuple()
    {
        static_assert(ord < numElements);
        if constexpr(std::is_same_v<U, std::tuple_element_t<ord, DeduplicatedTy>>)
        {
            return ord;
        }
        else
        {
            return FindOrdInDeduplicatedTuple<ord + 1, U>();
        }
    }

    template<typename U>
    static constexpr size_t typeOrdinal = FindOrdInDeduplicatedTuple<0, U>();

    template<size_t ord>
    static constexpr size_t remapOrdinal = typeOrdinal<std::tuple_element_t<ord, T>>;
};

template<typename V, typename... T>
constexpr auto make_array(T&&... t) -> std::array<V, sizeof...(T)>
{
    return {{ std::forward<T>(t)... }};
}


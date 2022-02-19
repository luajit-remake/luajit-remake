#include "common.h"

namespace CommonUtils
{

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

}   // namespace CommonUtils

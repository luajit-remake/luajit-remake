#pragma once

#include <cstdint>
#include <utility>

// Copied from LLVM::function_ref
//
// An efficient, type-erasing, non-owning reference to a callable. This is
// intended for use as the type of a function parameter that is not used
// after the function in question returns.
//
// This class does not own the callable, so it is not in general safe to store
// a function_ref.
//
// ***WARNING***: NEVER EVER WRITE
//     FunctionRef<void(void)> fn = [&](){...};
// The lambda above is destructed at the end of the full-expression so fn will be
// holding a dangling reference!
//
// For recursive lambda, you need to declare FunctionRef first, then declare
// the lambda using 'auto' and capture the FunctionRef by reference, and finally
// assign lambda to FunctionRef.
//
template<typename Fn> class FunctionRef;

template<typename Ret, typename... Params>
class FunctionRef<Ret(Params...)>
{
    using CallbackFn = Ret(*)(void* /*callable*/, Params...);
    CallbackFn m_callback;
    void* m_callable;

    template<typename Callable>
    static Ret callback_fn(void* callable, Params... params)
    {
        return (*reinterpret_cast<Callable*>(callable))(
                    std::forward<Params>(params)...);
    }

public:
    FunctionRef() : m_callback(nullptr) {}
    FunctionRef(std::nullptr_t) : m_callback(nullptr) {}

    template <typename Callable>
    FunctionRef(Callable&& callable,
                std::enable_if_t<
                    !std::is_same<
                        std::remove_cv_t<std::remove_reference_t<Callable>>, FunctionRef
                    >::value>* = nullptr)
        : m_callback(callback_fn<typename std::remove_reference<Callable>::type>)
        , m_callable(&callable)
    { }

    Ret operator()(Params ...params) const
    {
        return m_callback(m_callable, std::forward<Params>(params)...);
    }

    explicit operator bool() const { return m_callback; }
};

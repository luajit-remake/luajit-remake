#pragma once

#include "tvalue.h"

namespace DeegenAPI
{

using namespace ToyLang;

// Return zero or one value as the result of the operation
//
void NO_RETURN Return(TValue value);
void NO_RETURN Return();

namespace detail {

template<typename Lambda>
inline constexpr auto lambda_functor_member_pointer_v = &Lambda::operator();

template<typename Lambda>
constexpr const void* lambda_functor_member_pointer_pointer_v = &lambda_functor_member_pointer_v<Lambda>;

void ImplPreserveLambdaInfo(const void* /*closure state address*/, const void* /*closure function address's address*/);

template<typename Lambda>
void NO_INLINE PreserveLambdaInfo(const Lambda& lambda)
{
    ImplPreserveLambdaInfo(static_cast<const void*>(&lambda), lambda_functor_member_pointer_pointer_v<Lambda>);
}

enum class SwitchCaseTag : int32_t { tag = 1 };
enum class SwitchDefaultTag : int32_t { tag = 2 };

constexpr SwitchCaseTag x_switchCaseTag = SwitchCaseTag::tag;
constexpr SwitchDefaultTag x_switchDefaultTag = SwitchDefaultTag::tag;

template<typename LambdaCond, typename LambdaAction>
void NO_INLINE MakeSwitchCaseClause(const LambdaCond& lambdaCond, const LambdaAction& lambdaAction)
{
    static_assert(std::is_invocable_v<LambdaCond>, "switch-case case clause must be a lambda with no parameters");
    static_assert(std::is_same_v<std::invoke_result_t<LambdaCond>, bool>, "switch-case case clause must return bool");
    static_assert(std::is_invocable_v<LambdaAction>, "switch-case action clause must be a lambda with no parameters");
    static_assert(std::is_same_v<std::invoke_result_t<LambdaAction>, void>, "switch-case action clause must return void");
    PreserveLambdaInfo(lambdaCond);
    PreserveLambdaInfo(lambdaAction);
}

template<typename LambdaAction>
void NO_INLINE MakeSwitchDefaultClause(const LambdaAction& lambdaAction)
{
    static_assert(std::is_invocable_v<LambdaAction>, "switch-case action clause must be a lambda with no parameters");
    static_assert(std::is_same_v<std::invoke_result_t<LambdaAction>, void>, "switch-case action clause must return void");
    PreserveLambdaInfo(lambdaAction);
}

inline void ALWAYS_INLINE MakeSwitch() { }

template<typename LambdaCond, typename LambdaAction, typename... Args>
void NO_INLINE MakeSwitch(const SwitchCaseTag&, const LambdaCond& lambdaCond, const LambdaAction& lambdaAction, const Args&... args)
{
    MakeSwitchCaseClause(lambdaCond, lambdaAction);
    MakeSwitch(args...);
}

template<typename LambdaAction, typename... Args>
void NO_INLINE MakeSwitch(const SwitchDefaultTag&, const LambdaAction& lambdaAction, const Args&... args)
{
    MakeSwitchDefaultClause(lambdaAction);
    MakeSwitch(args...);
}

}   // namespace detail

struct SwitchOnMutuallyExclusiveCases
{
    template<typename... Args>
    NO_INLINE SwitchOnMutuallyExclusiveCases(const Args&... args)
    {
        detail::MakeSwitch(args...);
    }
};

#define CASE(...) ::DeegenAPI::detail::x_switchCaseTag, [&]() -> bool { return (__VA_ARGS__); }, [&]() -> void
#define DEFAULT() ::DeegenAPI::detail::x_switchDefaultTag, [&]() -> void

}   // namespace DeegenAPI

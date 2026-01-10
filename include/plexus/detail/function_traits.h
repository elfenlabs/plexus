#pragma once
#include "plexus/node.h"
#include <cstddef>
#include <tuple>
#include <type_traits>

namespace Plexus {
    namespace detail {

        /**
         * @brief Template metaprogramming utility to extract function signature information.
         *
         * This is used to inspect lambda and function object parameter types at
         * compile-time for automatic dependency inference.
         */
        template <typename Func>
        struct function_traits;

        // Specialization for const member function (captures const lambdas)
        template <typename Ret, typename Class, typename... Args>
        struct function_traits<Ret (Class::*)(Args...) const> {
            using arg_tuple = std::tuple<Args...>;
            static constexpr std::size_t arity = sizeof...(Args);

            template <std::size_t N>
            using arg_type = std::tuple_element_t<N, arg_tuple>;
        };

        // Specialization for non-const member function
        template <typename Ret, typename Class, typename... Args>
        struct function_traits<Ret (Class::*)(Args...)> {
            using arg_tuple = std::tuple<Args...>;
            static constexpr std::size_t arity = sizeof...(Args);

            template <std::size_t N>
            using arg_type = std::tuple_element_t<N, arg_tuple>;
        };

        // Deduce from operator() for lambdas and functors
        template <typename Func>
        struct function_traits : function_traits<decltype(&Func::operator())> {};

        /**
         * @brief Infers access type (READ or WRITE) from function parameter type.
         *
         * Rules:
         * - const T& → Access::READ
         * - T& → Access::WRITE
         * - T (by value) → Access::WRITE (assumes modification intent)
         *
         * @tparam Idx The parameter index to inspect.
         * @tparam Func The function type to inspect.
         * @return constexpr Access The inferred access type.
         */
        template <std::size_t Idx, typename Func>
        constexpr Access infer_access_at() {
            using ArgType = typename function_traits<Func>::template arg_type<Idx>;
            using BaseType = std::remove_reference_t<ArgType>;

            if constexpr (std::is_const_v<BaseType>) {
                return Access::READ;
            } else {
                return Access::WRITE;
            }
        }

    } // namespace detail
} // namespace Plexus

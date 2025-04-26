/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_EXPRESSION_H
#define REACTION_EXPRESSION_H

#include "reaction/resource.h"
#include "reaction/triggerMode.h"

namespace reaction {

template <typename Op, typename L, typename R>
class BinaryOpExpr {
public:
    using ValueType = typename std::common_type_t<typename L::ValueType, typename R::ValueType>;
    template <typename Left, typename Right>
    BinaryOpExpr(Left &&l, Right &&r, Op o = Op{}) :
        left(std::forward<Left>(l)), right(std::forward<Right>(r)), op(o) {
    }

    auto operator()() const {
        return calculate();
    }

    auto calculate() const {
        return op(left(), right());
    }

    operator ValueType() {
        return calculate();
    }

private:
    L left;
    R right;
    Op op;
};

struct AddOp {
    auto operator()(auto &&l, auto &&r) const {
        return l + r;
    }
};

struct MulOp {
    auto operator()(auto &&l, auto &&r) const {
        return l * r;
    }
};

struct SubOp {
    auto operator()(auto &&l, auto &&r) const {
        return l - r;
    }
};

struct DivOp {
    auto operator()(auto &&l, auto &&r) const {
        return l / r;
    }
};

template <typename T>
struct ValueWrapper {
    using ValueType = T;
    T value;

    template <typename Type>
    ValueWrapper(Type &&t) :
        value(std::forward<Type>(t)) {
    }
    const T &operator()() const {
        return value;
    }
};

template <typename Op, typename L, typename R>
auto make_binary_expr(L &&l, R &&r) {
    return BinaryOpExpr<Op, ExprWrapper<std::decay_t<L>>, ExprWrapper<std::decay_t<R>>>(
        std::forward<L>(l),
        std::forward<R>(r));
}

template <typename L, typename R>
    requires CustomOpCC<L, R>
auto operator+(L &&l, R &&r) {
    return make_binary_expr<AddOp>(std::forward<L>(l), std::forward<R>(r));
}

template <typename L, typename R>
    requires CustomOpCC<L, R>
auto operator*(L &&l, R &&r) {
    return make_binary_expr<MulOp>(std::forward<L>(l), std::forward<R>(r));
}

template <typename L, typename R>
    requires CustomOpCC<L, R>
auto operator-(L &&l, R &&r) {
    return make_binary_expr<SubOp>(std::forward<L>(l), std::forward<R>(r));
}

template <typename L, typename R>
    requires CustomOpCC<L, R>
auto operator/(L &&l, R &&r) {
    return make_binary_expr<DivOp>(std::forward<L>(l), std::forward<R>(r));
}

// General Expression class template (for functor-based expressions)
template <typename TriggerPolicy, typename Fun, typename... Args>
class Expression : public Resource<ComplexExpr, ReturnType<Fun, Args...>>, public TriggerPolicy {
public:
    using ValueType = ReturnType<Fun, Args...>;
    using ExprType = ComplexExpr;

protected:
    // Sets the source data for the expression and updates the functor
    template <typename F, typename... A>
    ReactionError setSource(F &&f, A &&...args) {
        if constexpr (std::convertible_to<ReturnType<std::decay_t<F>, typename is_data_weak_ref<std::decay_t<A>>::Type...>, ValueType>) {
            if (!this->updateObservers(std::forward<A>(args)...)) {
                return ReactionError::CycleDepErr;
            }

            setFunctor(createGetFunRef(std::forward<F>(f), std::forward<A>(args)...));

            if constexpr (!std::is_void_v<ValueType>) {
                this->updateValue(evaluate());
            } else {
                evaluate();
            }
        }

        else {
            return ReactionError::ReturnTypeErr;
        }
        return ReactionError::NoErr;
    }

    void updateOneOb(DataNodePtr node) {
        this->updateOneObserver(node);
    }

    // Evaluates the functor and returns its result
    auto evaluate() const {
        if constexpr (VoidCC<ValueType>) {
            std::invoke(m_fun);
        } else {
            return std::invoke(m_fun);
        }
    }

    // Value change notification based on trigger policy
    void valueChanged(bool changed) {
        if constexpr (std::is_same_v<TriggerPolicy, ChangedTrigger>) {
            this->setChanged(changed);
        }

        if (TriggerPolicy::checkTrigger()) {
            if constexpr (!VoidCC<ValueType>) {
                auto oldVal = this->getValue();
                auto newVal = evaluate();
                this->updateValue(newVal);
                if constexpr (CompareCC<ValueType>) {
                    this->notifyObservers(oldVal != newVal);
                } else {
                    this->notifyObservers(true);
                }
            } else {
                evaluate();
            }
        }
    }

    // Set the functor for the expression
    void setFunctor(std::function<ValueType()> &&fun) {
        m_fun = std::move(fun);
    }

private:
    std::function<ValueType()> m_fun; // Functor for evaluation
};

// Specialized Expression class template (for simple value-based expressions)
template <typename TriggerPolicy, UnInvocaCC Type>
    requires(!IsBinaryOpExprCC<Type>)
class Expression<TriggerPolicy, Type> : public Resource<SimpleExpr, std::decay_t<Type>> {
public:
    using Resource<SimpleExpr, std::decay_t<Type>>::Resource;
    using ValueType = Type;
    using ExprType = SimpleExpr;

protected:
    // Direct value retrieval for simple expressions
    Type evaluate() const {
        return this->getValue();
    }
};

template <typename TriggerPolicy, typename Op, typename L, typename R>
class Expression<TriggerPolicy, BinaryOpExpr<Op, L, R>>
    : public Expression<AlwaysTrigger, std::function<typename std::common_type_t<typename L::ValueType, typename R::ValueType>()>> {
public:
    using ValueType = typename std::common_type_t<typename L::ValueType, typename R::ValueType>;
    using ExprType = SimpleExpr;
    template <typename T>
    Expression(T &&expr) :
        m_expr(std::forward<T>(expr)) {
    }

protected:
    ReactionError setOpExpr() {
        this->setFunctor([this]() {
            return m_expr.calculate();
        });
        this->updateValue(this->evaluate());
        return ReactionError::NoErr;
    }

private:
    BinaryOpExpr<Op, L, R> m_expr;
};

} // namespace reaction

#endif // REACTION_EXPRESSION_H

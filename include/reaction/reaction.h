/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_REACTION_H
#define REACTION_REACTION_H

#include "reaction/dataSource.h"

namespace reaction {
// Field alias for DataWeakRef to work with DataSource
template <typename SourceType>
using Field = DataWeakRef<DataSource<AlwaysTrigger, FieldStrategy, FieldIdentity<std::decay_t<SourceType>>>>;

// Function to create a Field DataSource
template <typename SourceType>
auto field(FieldBase *obj, SourceType &&value) {
    auto ptr = std::make_shared<DataSource<AlwaysTrigger, FieldStrategy, FieldIdentity<std::decay_t<SourceType>>>>
               (FieldIdentity<std::decay_t<SourceType>>{obj, std::forward<SourceType>(value)});
    FieldGraph::getInstance().addNode(ptr);
    return DataWeakRef{ptr.get()};
}

// Function to create a constant DataSource
template <TriggerCC TriggerPolicy = AlwaysTrigger, VarInvalidCC InvalidStrategy = DirectCloseStrategy, typename SourceType>
auto constVar(SourceType &&value) {
    auto ptr = std::make_shared<DataSource<TriggerPolicy, InvalidStrategy, const std::decay_t<SourceType>>>(std::forward<SourceType>(value));
    ObserverGraph::getInstance().addNode(ptr);
    return DataWeakRef{ptr.get()};
}

// Function to create a var DataSource
template <TriggerCC TriggerPolicy = AlwaysTrigger, VarInvalidCC InvalidStrategy = DirectCloseStrategy, typename SourceType>
auto var(SourceType &&value) {
    auto ptr = std::make_shared<DataSource<TriggerPolicy, InvalidStrategy, std::decay_t<SourceType>>>(std::forward<SourceType>(value));
    if constexpr (HasFieldCC<std::decay_t<SourceType>>) {
        ptr->setField();
    }
    ObserverGraph::getInstance().addNode(ptr);
    return DataWeakRef{ptr.get()};
}

template <TriggerCC TriggerPolicy = AlwaysTrigger, InvalidCC InvalidStrategy = DirectCloseStrategy, IsBinaryOpExprCC OpExpr>
auto expr(OpExpr &&opExpr) {
    auto ptr = std::make_shared<DataSource<TriggerPolicy, InvalidStrategy, std::decay_t<OpExpr>>>(std::forward<OpExpr>(opExpr));
    ObserverGraph::getInstance().addNode(ptr);
    ptr->set();
    return DataWeakRef{ptr.get()};
}

// Function to create a variable DataSource
template <TriggerCC TriggerPolicy = AlwaysTrigger, InvalidCC InvalidStrategy = DirectCloseStrategy, typename Fun, typename... Args>
auto calc(Fun &&fun, Args &&...args) {
    auto ptr = std::make_shared<DataSource<TriggerPolicy, InvalidStrategy, std::decay_t<Fun>, typename is_data_weak_ref<std::decay_t<Args>>::Type...>>();
    ObserverGraph::getInstance().addNode(ptr);
    ptr->set(std::forward<Fun>(fun), std::forward<Args>(args)...);
    return DataWeakRef{ptr.get()};
}

// Function to create an action DataSource
template <TriggerCC TriggerPolicy = AlwaysTrigger, InvalidCC InvalidStrategy = DirectCloseStrategy, typename Fun, typename... Args>
auto action(Fun &&fun, Args &&...args) {
    return calc<TriggerPolicy, InvalidStrategy>(std::forward<Fun>(fun), std::forward<Args>(args)...);
}

} // namespace reaction

#endif // REACTION_REACTION_H
/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

 #ifndef REACTION_CONCEPT_H
 #define REACTION_CONCEPT_H

 #include <type_traits>
 #include <memory>
 #include <concepts>

 namespace reaction {

 // Forward declarations
 struct DataNode;
 struct ActionNode;
 struct FieldNode;
 struct FieldBase;
 struct LastValStrategy;
 struct AnyType;
 struct SimpleExpr;
 class ObserverDataNode;
 class ObserverActionNode;
 class ObserverFieldNode;

 template <typename TriggerPolicy, typename InvalidStrategy, typename Type, typename... Args>
 class DataSource;

 template <typename Op, typename L, typename R>
 class BinaryOpExpr;

 template <typename T>
 class DataWeakRef;

 template <typename T>
 struct ValueWrapper;

 // ==================== Helper Types ====================

 struct AnyType {
     template <typename T>
     operator T() { return T{}; }
 };

 // ==================== Type Traits ====================

 template <typename T>
 struct is_binary_op_expr : std::false_type {};

 template <typename Op, typename L, typename R>
 struct is_binary_op_expr<BinaryOpExpr<Op, L, R>> : std::true_type {};

 template <typename T>
 struct is_data_weak_ref : std::false_type {
     using Type = T;
 };

 template <typename T>
 struct is_data_weak_ref<DataWeakRef<T>> : std::true_type {
     using Type = T;
 };

// ==================== Basic type concepts ====================

 template <typename T>
 concept ConstCC = std::is_const_v<std::decay_t<T>>;

 template <typename T>
 concept InvocaCC = std::is_invocable_v<std::decay_t<T>>;

 template <typename T>
 concept UnInvocaCC = !InvocaCC<T>;

 template <typename T>
 concept VoidCC = std::is_void_v<std::decay_t<T>>;

 template <typename... Args>
 concept ArgNonEmptyCC = sizeof...(Args) > 0;

 template <typename T>
 concept CompareCC = requires(T &a, T &b) {
     { a == b } -> std::convertible_to<bool>;
 };

 // ==================== Expression Traits ====================

 template <typename T>
 struct ExpressionTraits {
     using Type = T;
 };

 template <typename TriggerPolicy, typename InvalidStrategy, UnInvocaCC T>
 struct ExpressionTraits<DataSource<TriggerPolicy, InvalidStrategy, T>> {
     using Type = T;
 };

 template <typename TriggerPolicy, typename InvalidStrategy, typename Fun, typename... Args>
 struct ExpressionTraits<DataSource<TriggerPolicy, InvalidStrategy, Fun, Args...>> {
     using Type = decltype(std::declval<Fun>()(std::declval<typename ExpressionTraits<Args>::Type>()...));
 };

 template <typename Fun, typename... Args>
 using ReturnType = typename ExpressionTraits<DataSource<void, void, std::decay_t<Fun>, std::decay_t<Args>...>>::Type;

 template <typename T>
 using ExprWrapper = std::conditional_t<
     is_data_weak_ref<T>::value || is_binary_op_expr<T>::value,
     T,
     ValueWrapper<T>>;

 // ==================== Logic Concepts ====================

 template <typename T>
 concept NodeCC = requires {
     requires std::is_same_v<typename T::SourceType, DataNode> ||
              std::is_same_v<typename T::SourceType, ActionNode> ||
              std::is_same_v<typename T::SourceType, FieldNode>;
 };

 template <typename T>
 concept SourceCC = requires(T t) {
     requires requires { { t.getShared() } -> std::same_as<std::shared_ptr<ObserverDataNode>>; } ||
              requires { { t.getShared() } -> std::same_as<std::shared_ptr<ObserverActionNode>>; } ||
              requires { { t.getShared() } -> std::same_as<std::shared_ptr<ObserverFieldNode>>; };
 };

 template <typename T>
 concept DataSourceCC = requires {
     typename T::ValueType;
     requires SourceCC<T> && !std::is_void_v<typename T::ValueType>;
 };

 template <typename T>
 concept ActionSourceCC = requires {
     typename T::ValueType;
     requires SourceCC<T> && std::is_void_v<typename T::ValueType>;
 };

 template <typename T>
 concept SimpleExprCC = std::is_same_v<T, SimpleExpr>;

 template <typename T>
 concept TriggerCC = requires(T t) {
     { t.checkTrigger() } -> std::same_as<bool>;
 };

 template <typename T>
 concept InvalidCC = requires(T t, AnyType ds) {
     { t.handleInvalid(ds) } -> std::same_as<void>;
 };

 template <typename T>
 concept VarInvalidCC = InvalidCC<T> && !std::is_same_v<std::decay_t<T>, LastValStrategy>;

 template <typename T>
 concept HasFieldCC = std::is_base_of_v<FieldBase, std::decay_t<T>>;

 template <typename T>
 concept IsBinaryOpExprCC = is_binary_op_expr<T>::value;

 template <typename L, typename R>
 concept CustomOpCC = is_data_weak_ref<std::decay_t<L>>::value ||
                     is_data_weak_ref<std::decay_t<R>>::value ||
                     is_binary_op_expr<std::decay_t<L>>::value ||
                     is_binary_op_expr<std::decay_t<R>>::value;

 } // namespace reaction

 #endif // REACTION_CONCEPT_H
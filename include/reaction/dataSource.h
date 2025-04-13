/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_DATASOURCE_H
#define REACTION_DATASOURCE_H

#include "reaction/expression.h"
#include "reaction/invalidStrategy.h"
#include <atomic>

namespace reaction {

inline thread_local bool reg_flg = false;
inline thread_local std::function<void(DataNodePtr)> reg_fun;

struct RegGuard {
    RegGuard() {
        reg_flg = false;
    }
    ~RegGuard() {
        reg_flg = false;
        reg_fun = nullptr;
    }
};

template <typename DataType>
class DataWeakRef {
public:
    using InvStrategy = typename DataType::InvStrategy;
    using ValueType = typename DataType::ValueType;

    // Constructor: increases weak reference count
    explicit DataWeakRef(DataType *ptr) :
        m_rawPtr(ptr) {
        if (m_rawPtr) {
            m_rawPtr->addWeakRef(this, m_cb); // Increment weak reference count in DataType
        }
    }

    // Destructor: decreases weak reference count
    ~DataWeakRef() {
        if (m_rawPtr) {
            m_rawPtr->releaseWeakRef(this); // Decrement weak reference count in DataType
        }
    }

    // Copy constructor: increases weak reference count
    DataWeakRef(const DataWeakRef &other) :
        m_rawPtr(other.m_rawPtr) {
        if (m_rawPtr) {
            m_rawPtr->addWeakRef(this, m_cb); // Increment weak reference count in DataType
        }
    }

    // Move constructor: transfers ownership, nullifies the source pointer
    DataWeakRef(DataWeakRef &&other) noexcept :
        m_rawPtr(other.m_rawPtr) {
        other.m_rawPtr = nullptr; // Nullify the moved-from object
    }

    // Copy assignment: increases weak reference count after releasing current reference
    DataWeakRef &operator=(const DataWeakRef &other) noexcept {
        if (this != &other) {
            if (m_rawPtr) {
                m_rawPtr->releaseWeakRef(this); // Decrement weak reference count
            }
            m_rawPtr = other.m_rawPtr;
            if (m_rawPtr) {
                m_rawPtr->addWeakRef(this, m_cb); // Increment weak reference count
            }
        }
        return *this;
    }

    // Move assignment: transfers ownership, nullifies the source pointer
    DataWeakRef &operator=(DataWeakRef &&other) noexcept {
        if (this != &other) {
            if (m_rawPtr) {
                m_rawPtr->releaseWeakRef(this); // Decrement weak reference count
            }
            m_rawPtr = other.m_rawPtr;
            other.m_rawPtr = nullptr; // Nullify the moved-from object
        }
        return *this;
    }

    // Arrow operator: accesses raw pointer's members
    auto operator->() const {
        return getPtr()->getRaw();
    }

    auto operator()() const {
        if constexpr (!std::is_same_v<typename DataType::InvStrategy, FieldStrategy>) {
            if (reg_flg && m_rawPtr) {
                std::invoke(reg_fun, m_rawPtr->getShared()); // Call registration function
            }
        }
        return get();
    }

    // Dereference operator: returns the underlying object
    DataType &operator*() const {
        return *getPtr(); // Dereference the raw pointer
    }

    // Explicit conversion to bool: checks if the raw pointer is valid
    explicit operator bool() const {
        return m_rawPtr != nullptr; // Returns true if the pointer is not null
    }

    // Getter functions for accessing data from the DataSource
    auto get() const
        requires DataSourceCC<DataType>
    {
        return getPtr()->getValue(); // Get the value of the data source
    }

    auto getUpdate() const
        requires DataSourceCC<DataType>
    {
        return getPtr()->evaluate(); // Get the updated value of the data source
    }

    auto getRaw() const
        requires DataSourceCC<DataType>
    {
        return getPtr()->getRaw(); // Get the raw value of the data source
    }

    decltype(auto) getRef() const
        requires DataSourceCC<DataType>
    {
        return getPtr()->getRef(); // Get the reference to the data source
    }

    auto getShared() {
        return getPtr()->getShared(); // Get a shared pointer to the data source
    }

    template <typename T>
    void value(T &&t) {
        getPtr()->value(std::forward<T>(t));
    }

    template <typename F, typename... A>
    bool set(F &&f, A &&...args) {
        return getPtr()->set(std::forward<F>(f), std::forward<A>(args)...); // Set value in the data source
    }

    // Set a threshold for the source
    template <typename F, typename... A>
    void setThreshold(F &&f, A &&...args) {
        getPtr()->setThreshold(std::forward<F>(f), std::forward<A>(args)...); // Set threshold for the data source
    }

    // Close the data source
    void close() {
        getPtr()->close(); // Close the data source
    }

    // Set and get the name of the data source
    void setName(const std::string &name) {
        getPtr()->setName(name); // Set the name of the data source
    }

    std::string getName() const {
        return getPtr()->getName(); // Get the name of the data source
    }

private:
    // Helper function to safely access the raw pointer, throws if null
    DataType *getPtr() const {
        if (!m_rawPtr) {
            throw std::runtime_error("Null weak pointer access"); // Throws if the pointer is null
        }
        return m_rawPtr; // Returns the raw pointer
    }

    // Raw pointer to the DataSource object
    DataType *m_rawPtr = nullptr; // Initialize the pointer to null
    std::function<void()> m_cb = [this]() {
        m_rawPtr = nullptr;
    };
};

// DataSource class template that handles the value, observers, and invalidation strategies
template <typename TriggerPolicy, typename InvalidStrategy, typename Type, typename... Args>
class DataSource : public Expression<TriggerPolicy, Type, Args...>,
                   private InvalidStrategy {
public:
    using Expr = Expression<TriggerPolicy, Type, Args...>;
    using ValueType = typename Expr::ValueType;
    using InvStrategy = InvalidStrategy;
    using Expr::Expr; // Inherit constructors from Expr

    ~DataSource() {
        notifyDestruction();
    }

    // Set new value and notify observers
    template <typename F, ArgNonEmptyCC... A>
    bool set(F &&f, A &&...args) {
        return this->setSource(std::forward<F>(f), std::forward<A>(args)...);
    }

    template <InvocaCC F>
    bool set(F &&f) {
        RegGuard guard;
        reg_flg = true;
        reg_fun = [this](DataNodePtr node) {
            this->updateOneOb(node);
        };
        return this->setSource(std::forward<F>(f));
    }

    bool set() {
        RegGuard guard;
        reg_flg = true;
        reg_fun = [this](DataNodePtr node) {
            this->updateOneOb(node);
        };
        this->setOpExpr();
        return true;
    }

    // Close the DataSource and remove it from the observer graph
    void close() {
        ObserverGraph::getInstance().closeNode(this->getShared());
    }

    // Assignment operator to update the value and notify observers
    template <typename T>
    DataSource &operator=(T &&t)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        value(std::forward<T>(t));
        return *this;
    }

    // Addition assignment operator
    template <typename T>
    DataSource &operator+=(const T &rhs)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() + rhs);
        this->notifyObservers(true);
        return *this;
    }

    // Subtraction assignment operator
    template <typename T>
    DataSource &operator-=(const T &rhs)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() - rhs);
        this->notifyObservers(true);
        return *this;
    }

    // Multiplication assignment operator
    template <typename T>
    DataSource &operator*=(const T &rhs)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() * rhs);
        this->notifyObservers(true);
        return *this;
    }

    // Division assignment operator
    template <typename T>
    DataSource &operator/=(const T &rhs)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() / rhs);
        this->notifyObservers(true);
        return *this;
    }

    // Prefix increment operator
    DataSource &operator++()
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() + 1);
        this->notifyObservers(true);
        return *this;
    }

    // Prefix decrement operator
    DataSource &operator--()
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(this->getValue() - 1);
        this->notifyObservers(true);
        return *this;
    }

    template <typename T>
    void value(T &&t)
        requires(!ConstCC<ValueType> && !VoidCC<ValueType>)
    {
        this->updateValue(std::forward<T>(t));
        if constexpr (CompareCC<ValueType>) {
            this->notifyObservers(this->getValue() != t);
        }
        else {
            this->notifyObservers(true);
        }
    }

    // Getter functions
    auto get() const {
        return this->getValue();
    }
    auto getUpdate() const {
        return this->evaluate();
    }
    auto getRaw() const {
        return this->getRawPtr();
    }
    decltype(auto) getRef() const {
        return this->getReference();
    }

private:
    // Friend class to allow access to private methods for weak reference management
    template <typename T>
    friend class DataWeakRef;

    // Methods for managing weak references
    void addWeakRef(DataWeakRef<DataSource> *ptr, std::function<void()> &f) {
        m_weakRefCount++;
        m_destructionCallbacks.insert({ptr, f});
    }

    void releaseWeakRef(DataWeakRef<DataSource> *ptr) {
        --m_weakRefCount;
        m_destructionCallbacks.erase(ptr);
        if (m_weakRefCount == 0) {
            this->handleInvalid(*this);
        }
    }

    void notifyDestruction() {
        for (auto &[ptr, cb] : m_destructionCallbacks) {
            cb();
        }
    }
    // Atomic counter for weak references
    std::atomic<int> m_weakRefCount{0};
    std::unordered_map<DataWeakRef<DataSource> *, std::function<void()>> m_destructionCallbacks;
};

} // namespace reaction

#endif // REACTION_DATASOURCE_H

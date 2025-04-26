/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_RESOURCE_H
#define REACTION_RESOURCE_H

#include "reaction/observerNode.h"
#include <exception>

namespace reaction {

// Base struct for fields (empty base class)
struct FieldBase {
    ~FieldBase() {
        FieldGraph::getInstance().deleteObj(this);
    }
};

// Struct to hold Objdata and value for a field identity
template <typename Type>
struct FieldIdentity {
    // Constructor to initialize Objdata pointer and value
    template <typename T>
    FieldIdentity(FieldBase *ptr, T &&t) :
        ObjPtr(ptr), value(std::forward<T>(t)) {
    }

    FieldBase *ObjPtr; // Pointer to Objdata
    Type value;               // Field value
};

// Types to differentiate between simple and complex expressions
struct SimpleExpr {};
struct ComplexExpr {};

// Base class for resources that holds the actual value of a type
template <typename ExprType, typename T>
class ResourceBase {
public:
    // Default constructor that initializes the resource with a null pointer
    ResourceBase() :
        m_ptr(nullptr) {
    }

    // Constructor that initializes the resource with a value
    template <typename Type>
    ResourceBase(Type &&t) :
        m_ptr(std::make_unique<T>(std::forward<Type>(t))) {
    }

protected:
    // Getter for the value, throws if the pointer is null
    T getValue() const {
        if (!m_ptr) {
            throw std::runtime_error("Attempt to get a null pointer");
        }
        return *m_ptr;
    }

    // Updates the value, creates a new one if the pointer is null
    template <typename Type>
    void updateValue(Type &&t) {
        if (!m_ptr) {
            m_ptr = std::make_unique<T>(std::forward<Type>(t));
        } else {
            *m_ptr = std::forward<Type>(t);
        }
    }

    // Returns a raw pointer to the value, throws if the pointer is null
    T *getRawPtr() const {
        if (!this->m_ptr) {
            throw std::runtime_error("Attempt to get a null pointer");
        }
        return this->m_ptr.get();
    }

    // Returns a reference to the value, throws if the pointer is null
    T &getReference() const {
        if (!m_ptr) {
            throw std::runtime_error("Attempt to get a null pointer");
        }
        return *m_ptr;
    }

    std::unique_ptr<T> m_ptr; // Unique pointer holding the value
};

// Resource class template for complex expression types
template <typename ExprType, typename T>
class Resource : public ResourceBase<ExprType, T>, public ObserverDataNode {
public:
    // Inherit constructors from ResourceBase
    using ResourceBase<ExprType, T>::ResourceBase;
};

// Specialization of Resource for SimpleExpr type and T type with field
template <HasFieldCC T>
class Resource<SimpleExpr, T> : public ResourceBase<ComplexExpr, T>, public ObserverDataNode {
public:
    // Inherit constructors from ResourceBase
    using ResourceBase<ComplexExpr, T>::ResourceBase;

protected:
    // Set the field in the FieldGraph
    void setField() {
        FieldGraph::getInstance().setField(this->getRawPtr(), this->getShared());
    }
    template <TriggerCC TriggerPolicy, VarInvalidCC InvalidStrategy, typename SourceType>
    friend auto var(SourceType &&value);
};

// Specialization of Resource for SimpleExpr type and FieldIdentity type
template <typename T>
class Resource<SimpleExpr, FieldIdentity<T>> : public ResourceBase<SimpleExpr, T>, public ObserverFieldNode {
public:
    // Constructor to initialize FieldIdentity and add to the FieldGraph
    template <typename Type>
    Resource(Type &&identity) :
        ResourceBase<SimpleExpr, T>(identity.value) {
        FieldGraph::getInstance().addObj(identity.ObjPtr, static_cast<ObserverFieldNode *>(this));
    }
};

// Specialization of Resource for ComplexExpr type with void (for action nodes)
template <>
class Resource<ComplexExpr, void> : public ObserverActionNode {};

} // namespace reaction

#endif // REACTION_RESOURCE_H

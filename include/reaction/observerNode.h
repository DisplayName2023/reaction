/*
 * Copyright (c) 2025 Lummy
 *
 * This software is released under the MIT License.
 * See the LICENSE file in the project root for full details.
 */

#ifndef REACTION_OBSERVERNODE_H
#define REACTION_OBSERVERNODE_H

#include "reaction/concept.h"
#include "reaction/log.h"
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <variant>

namespace reaction {

// Forward declarations for different node types
struct DataNode {};
struct ActionNode {};
struct FieldNode {};

using DataNodePtr = std::shared_ptr<ObserverDataNode>;
using ActionNodePtr = std::shared_ptr<ObserverActionNode>;
using FieldNodePtr = std::shared_ptr<ObserverFieldNode>;

using NodeVariant = std::variant<DataNodePtr, ActionNodePtr>;

struct NodeVariantHash {
    std::size_t operator()(const NodeVariant &node) const {
        return std::visit([](auto &&ptr) -> std::size_t {
            return std::hash<typename std::decay<decltype(ptr)>::type>()(ptr);
        }, node);
    }
};

struct NodeVariantEqual {
    bool operator()(const NodeVariant &a, const NodeVariant &b) const {
        return a.index() == b.index() && std::visit([](auto &&ptr1, auto &&ptr2) -> bool {
                   using T1 = std::decay_t<decltype(ptr1)>;
                   using T2 = std::decay_t<decltype(ptr2)>;
                   if constexpr (std::is_same_v<T1, T2>) {
                       return ptr1 == ptr2;
                   } else {
                       return false;
                   }
               }, a, b);
    }
};

inline thread_local std::unordered_set<NodeVariant, NodeVariantHash, NodeVariantEqual> g_wait_list;

// FieldGraph handles field-specific operations
class FieldGraph {
public:
    static FieldGraph &getInstance() {
        static FieldGraph instance;
        return instance;
    }

    // Add a field node to the graph
    void addNode(FieldNodePtr node) {
        m_fieldList.insert(node);
    }

    // Close a field node in the graph
    void closeNode(FieldNodePtr node) {
        m_fieldList.erase(node);
    }

    // Add a field to the graph
    void addObj(FieldBase *obj, ObserverFieldNode *node) {
        m_fieldMap[obj].insert(node);
    }

    // Delete a field from the graph
    void deleteObj(FieldBase *obj) {
        m_fieldMap.erase(obj);
    }

    // Set the field associated with a objdata pointer
    void setField(FieldBase *obj, DataNodePtr objPtr) {
        for (auto node : m_fieldMap[obj]) {
            m_fieldObservers[objPtr].insert(node);
        }
    }

    void deleteObservers(DataNodePtr node) {
        m_fieldObservers.erase(node);
    }

    // Get the objdata for a field node
    std::unordered_set<ObserverFieldNode *> getObservers(DataNodePtr node) {
        auto it = m_fieldObservers.find(node);
        if (it != m_fieldObservers.end()) {
            return it->second;
        } else {
            return {};
        }
    }

private:
    FieldGraph() {
    }
    std::unordered_map<FieldBase *, std::unordered_set<ObserverFieldNode *>> m_fieldMap;
    std::unordered_map<DataNodePtr, std::unordered_set<ObserverFieldNode *>> m_fieldObservers;
    std::unordered_set<FieldNodePtr> m_fieldList;
};

// ObserverGraph handles dependencies between nodes and manages observers
class ObserverGraph {
public:
    // Singleton instance of ObserverGraph
    static ObserverGraph &getInstance() {
        static ObserverGraph instance;
        return instance;
    }

    // Add a new node to the graph
    template <NodeCC NodeType>
    void addNode(std::shared_ptr<NodeType> node) {
        m_dependents[node];
        m_observers[node];
    }

    // Add an observer for a specific node
    template <NodeCC NodeType, NodeCC TargetType>
    bool addObserver(std::shared_ptr<NodeType> source, std::shared_ptr<TargetType> target) {
        if constexpr (std::is_same_v<NodeType, TargetType>) {
            if (source == target) {
                Log::error("Cannot observe self, node = {}.", source->getName());
                return false;
            }
        }
        if constexpr (std::is_same_v<typename NodeType::SourceType, DataNode>) {
            if (hasCycle(source, target)) {
                Log::error("Cycle dependency detected, node = {}. Cycle dependent = {}", source->getName(), target->getName());
                return false;
            }

            if (auto obs = FieldGraph::getInstance().getObservers(target); !obs.empty()) {
                for (auto &ob : obs) {
                    ob->addOb(source);
                }
            }
        }

        hasRepeatDependencies(source, target);

        m_dependents[source].insert(target);
        m_observers[target].insert({source});
        target->addOb(source);
        return true;
    }

    // Reset the observers for a node
    template <NodeCC NodeType>
    void resetNode(std::shared_ptr<NodeType> node) {
        cleanupDependencies(node);
        m_dependents[node].clear();
        if (m_repeatDependencies.find(node) != m_repeatDependencies.end()) {
            for (auto &root : m_repeatDependencies[node]) {
                root->deleteWait(node);
            }
        }
        m_repeatDependencies.erase(node);
    }

    // Close a node and its dependencies
    template <NodeCC NodeType>
    void closeNode(std::shared_ptr<NodeType> node) {
        if (!node) return;
        std::unordered_set<std::shared_ptr<void>> closedNodes;
        cascadeCloseDependents(node, closedNodes);
    }

private:
    ObserverGraph() {
    }

    template <NodeCC NodeType>
    void cleanupDependencies(std::shared_ptr<NodeType> node) {
        for (auto dep : m_dependents[node]) {
            auto it = m_observers[dep].find(node);
            if (it != m_observers[dep].end()) {
                dep->deleteOb(*it);
                if constexpr (std::is_same_v<std::decay_t<decltype(*it)>, DataNodePtr>) {
                    if (auto obs = FieldGraph::getInstance().getObservers(dep); !obs.empty()) {
                        for (auto &ob : obs) {
                            ob->deleteCb(*it);
                        }
                    }
                }
            }
            m_observers[dep].erase(node);
        }
    }

    // Cascade close all dependent nodes
    template <NodeCC NodeType>
    void cascadeCloseDependents(std::shared_ptr<NodeType> node, std::unordered_set<std::shared_ptr<void>> &closedNodes) {
        if (!node || closedNodes.count(node)) return;
        closedNodes.insert(node);

        auto observers = m_observers[node];
        for (auto &ob : observers) {
            std::visit([&](auto &&ptr) {
                cascadeCloseDependents(ptr, closedNodes);
            }, ob);
        }

        closeNodeInternal(node);
    }

    // Internal close node implementation (without cascading)
    template <NodeCC NodeType>
    void closeNodeInternal(std::shared_ptr<NodeType> node) {
        if (!node) return;

        cleanupDependencies(node);
        m_dependents.erase(node);
        if constexpr (std::is_same_v<typename NodeType::SourceType, DataNode>) {
            for (auto &ob : m_observers[node]) {
                m_dependents[ob].erase(node);
            }
        }
        m_observers.erase(node);

        if (m_repeatDependencies.find(node) != m_repeatDependencies.end()) {
            for (auto &root : m_repeatDependencies[node]) {
                root->deleteWait(node);
            }
        }
        m_repeatDependencies.erase(node);
        node.reset();
    }

    // Check for cycle dependency between nodes
    bool hasCycle(DataNodePtr source, DataNodePtr target) {
        m_dependents[source].insert(target);
        m_observers[target].insert(source);

        std::unordered_set<DataNodePtr> visited;
        std::unordered_set<DataNodePtr> recursionStack;
        bool hasCycle = dfs(source, visited, recursionStack);

        m_dependents[source].erase(target);
        m_observers[target].erase(source);

        return hasCycle;
    }

    // Depth-first search to detect cycles
    bool dfs(DataNodePtr node, std::unordered_set<DataNodePtr> &visited, std::unordered_set<DataNodePtr> &recursionStack) {
        if (recursionStack.count(node)) return true;
        if (visited.count(node)) return false;

        visited.insert(node);
        recursionStack.insert(node);

        for (auto neighbor : m_dependents[node]) {
            if (dfs(neighbor, visited, recursionStack)) return true;
        }

        recursionStack.erase(node);
        return false;
    }

    template <NodeCC NodeType>
    void hasRepeatDependencies(std::shared_ptr<NodeType> source, DataNodePtr target) {
        std::unordered_set<DataNodePtr> targetDependencies;
        collectDependencies(target, targetDependencies);

        std::unordered_set<DataNodePtr> visited;
        std::unordered_set<DataNodePtr> dependents;
        for (auto &dependent : m_dependents[source]) {
            if (checkDependency(source, dependent, targetDependencies, visited)) {
                return;
            }
        }
    }

    // Collect all dependencies for a given node
    void collectDependencies(DataNodePtr node, std::unordered_set<DataNodePtr> &dependencies) {
        if (!node || dependencies.count(node)) return;
        dependencies.insert(node);

        for (auto neighbor : m_dependents[node]) {
            collectDependencies(neighbor, dependencies);
        }
    }

    // Check if a node is part of a target's dependencies
    template <NodeCC SrcType, NodeCC NodeType>
    bool checkDependency(std::shared_ptr<SrcType> source, std::shared_ptr<NodeType> node, const std::unordered_set<DataNodePtr> &targetDependencies, std::unordered_set<DataNodePtr> &visited) {
        if (visited.count(node)) return false;
        visited.insert(node);

        if (targetDependencies.count(node)) {
            m_repeatDependencies[source].insert(node);
            node->addWait(source);
            return true;
        }

        for (auto &dependent : m_dependents[node]) {
            if (checkDependency(source, dependent, targetDependencies, visited)) {
                return true;
            }
        }
        return false;
    }

    std::unordered_map<NodeVariant, std::unordered_set<NodeVariant, NodeVariantHash, NodeVariantEqual>, NodeVariantHash, NodeVariantEqual> m_observers;
    std::unordered_map<NodeVariant, std::unordered_set<DataNodePtr>, NodeVariantHash, NodeVariantEqual> m_dependents;
    std::unordered_map<NodeVariant, std::unordered_set<DataNodePtr, NodeVariantHash, NodeVariantEqual>> m_repeatDependencies;
};

// Base class for all observer nodes
template <typename Derived>
class ObserverBase : public std::enable_shared_from_this<Derived> {
public:
    using SourceType = DataNode;
    ObserverBase(const ObserverBase &) = delete;
    ObserverBase &operator=(const ObserverBase &) = delete;
    ObserverBase(ObserverBase &&) = delete;
    ObserverBase &operator=(ObserverBase &&) = delete;
    // Constructor with an optional name
    ObserverBase(const std::string &name = "") :
        m_name(name) {
    }

    // Set and get the name of the observer
    void setName(const std::string &name) {
        m_name = name;
    }
    std::string getName() const {
        return m_name;
    }

    // Get a shared pointer to the derived class
    std::shared_ptr<Derived> getShared() {
        return this->shared_from_this();
    }

    virtual void valueChanged([[maybe_unused]] bool changed) {
    }

protected:
    bool updateObservers(auto &&...args) {
        auto shared_this = getShared();
        ObserverGraph::getInstance().resetNode(shared_this);
        if (!(ObserverGraph::getInstance().addObserver(shared_this, args.getShared()) && ...)) {
            ObserverGraph::getInstance().resetNode(shared_this);
            return false;
        }
        return true;
    }

    void updateOneObserver(DataNodePtr node) {
        ObserverGraph::getInstance().addObserver(getShared(), node);
    }

    void notifyObservers(bool changed) {
        for (auto &observer : m_waitObservers) {
            g_wait_list.insert(observer);
        }

        for (auto observer : m_observers) {
            if (g_wait_list.find(observer) == g_wait_list.end()) {
                std::visit([&](auto &&ob) {
                    ob->valueChanged(changed);
                }, observer);
            }
        }

        if (!m_waitObservers.empty()) {
            for (auto&& observer : m_waitObservers) {
                std::visit([&](auto&& ob) { ob->valueChanged(changed); }, observer);
                g_wait_list.erase(observer);

            }
        }
    }

private:
    friend class ObserverGraph;
    void addOb(const NodeVariant &ob) {
        m_observers.emplace_back(ob);
    }

    void deleteOb(const NodeVariant &ob) {
        m_observers.erase(std::remove(m_observers.begin(), m_observers.end(), ob), m_observers.end());
    }

    void addWait(const NodeVariant &ob) {
        m_waitObservers.insert(ob);
    }

    void deleteWait(const NodeVariant &ob) {
        m_waitObservers.erase(m_waitObservers.find(ob));
    }

    std::string m_name;
    std::vector<NodeVariant> m_observers;
    std::unordered_set<NodeVariant, NodeVariantHash, NodeVariantEqual> m_waitObservers;
};

// ObserverDataNode handles data node-specific observers
class ObserverDataNode : public ObserverBase<ObserverDataNode> {
public:
    using SourceType = DataNode;
};

// ObserverActionNode handles action node-specific observers
class ObserverActionNode : public ObserverBase<ObserverActionNode> {
public:
    using SourceType = ActionNode;
};

// ObserverFieldNode handles field node-specific observers
class ObserverFieldNode : public ObserverBase<ObserverFieldNode> {
public:
    using SourceType = FieldNode;
};

} // namespace reaction

#endif // REACTION_OBSERVERNODE_H

#pragma once

#include <array>
#include <memory>
#include <utility>
#include <vector>
#include <type_traits>

#include <QVector3D>

struct IcoMesh;

// A vector-shaped immutable payload. Copies share the backing allocation;
// mutation detaches first so topology construction remains local and safe.
template<class T>
class SharedTopologyVector {
public:
    using value_type = T;
    using const_iterator = typename std::vector<T>::const_iterator;

    SharedTopologyVector() : data_(std::make_shared<std::vector<T>>()) {}
    SharedTopologyVector(const SharedTopologyVector&) = default;
    SharedTopologyVector& operator=(const SharedTopologyVector&) = default;

    size_t size() const noexcept { return data_->size(); }
    bool empty() const noexcept { return data_->empty(); }
    void reserve(size_t n) { detach(); data_->reserve(n); }
    void clear() { detach(); data_->clear(); }
    void push_back(const T& value) { detach(); data_->push_back(value); }
    void push_back(T&& value) { detach(); data_->push_back(std::move(value)); }
    void resize(size_t n) { detach(); data_->resize(n); }
    const T& operator[](size_t i) const { return (*data_)[i]; }
    const T& front() const { return data_->front(); }
    const T& back() const { return data_->back(); }
    const_iterator begin() const noexcept { return data_->begin(); }
    const_iterator end() const noexcept { return data_->end(); }
    std::shared_ptr<const std::vector<T>> sharedData() const noexcept { return data_; }

private:
    void detach() {
        if (data_.use_count() != 1) data_ = std::make_shared<std::vector<T>>(*data_);
    }
    std::shared_ptr<std::vector<T>> data_;
};

struct TopologyCell {
    int id = -1;
    bool isPentagon = false;
    SharedTopologyVector<int> poly;
    SharedTopologyVector<int> neighbors;
    QVector3D centroid;
    float area = 0.0f;
};

struct PickTri { // geometry for ray picking
    int cellId = -1;
    QVector3D v0, v1, v2;
};

// Immutable geometry and adjacency for one active subdivision.
class HexSphereTopology {
public:
    int subdivisionLevel = 0;
    int pentagonCount = 0;
    std::vector<QVector3D> dualVerts;
    std::vector<std::array<int, 3>> dualOwners;
    std::vector<TopologyCell> cells;
    std::vector<std::pair<int, int>> wireEdges;
    std::vector<PickTri> pickTris;

    int cellCount() const noexcept { return static_cast<int>(cells.size()); }
    int faceCount() const noexcept { return static_cast<int>(dualVerts.size()); }
};

using HexSphereTopologyPtr = std::shared_ptr<const HexSphereTopology>;

HexSphereTopologyPtr buildHexSphereTopology(int subdivisionLevel);

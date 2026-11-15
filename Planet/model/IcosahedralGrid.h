#pragma once

#include <array>
#include <cstdint>
#include <optional>

// An implicit, global frequency-n grid. Geometry is defined on the unit sphere;
// neither a spatial search tree nor a stored dual mesh is needed.
class IcosahedralGrid {
public:
    struct Point {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };

    // Frequency belongs to the grid: the complete address is (n, face, i, j).
    // Homogeneous weights on a base face are (n-i-j, i, j).
    struct Address {
        int face = 0;
        int i = 0;
        int j = 0;
        bool operator==(const Address&) const = default;
    };

    struct Neighbors {
        std::array<Address, 6> addresses{};
        int count = 0;
    };

    struct Cell {
        Point generator;
        std::array<Point, 6> vertices{};
        // Edge k runs from vertices[k] to vertices[(k+1)%degree].
        std::array<Address, 6> neighbors{};
        int degree = 0;
    };

    // The bound keeps integer addresses and the double-precision construction
    // well separated. Construction stores only n, irrespective of cell count.
    static constexpr int kMaxFrequency = 1 << 20;
    explicit IcosahedralGrid(int frequency);

    int frequency() const noexcept { return frequency_; }
    std::uint64_t cellCount() const noexcept;
    bool valid(Address address) const noexcept;

    // Shared base-edge/base-vertex addresses have one canonical owner and ID.
    Address canonical(Address address) const;
    std::uint64_t id(Address address) const;
    Address address(std::uint64_t id) const;
    Point point(Address address) const;

    // Direct Voronoi address: sign/symmetry reduction, integer triangle
    // coordinates, then two bisector-side predicates. No neighbor search.
    // See docs/icosahedral_grid.md for the conditions and derivation.
    // Exactly equidistant boundaries have a deterministic owner.
    Address locate(Point direction) const;
    // Ray and sphere are expressed in planet-local coordinates, with the
    // sphere centered at the origin. Camera/planet transforms stay with caller.
    std::optional<Address> locateRay(Point origin, Point direction, double radius = 1.0) const;

    Neighbors neighbors(Address address) const;
    Cell cell(Address address) const;

    // a,b must be neighboring primary nodes; a,b,c must be an incident triangle.
    Point edgeMidpoint(Address a, Address b) const;
    // The normal points into the Voronoi hemisphere belonging to a.
    Point bisectorNormal(Address a, Address b) const;
    Point dualVertex(Address a, Address b, Address c) const;

    static Point normalized(Point point);
    static double squaredChordDistance(Point a, Point b) noexcept;

private:
    struct Triangle { Address a, b, c; };
    struct IncidentTriangles {
        std::array<Triangle, 6> triangles{};
        int count = 0;
    };

    IncidentTriangles incidentTriangles(Address address) const;
    Point pointOnFace(Address address) const;
    int frequency_;
};

#include "tests/IcosahedralGridTests.h"
#include "model/IcosahedralGrid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Grid = IcosahedralGrid;
using Point = Grid::Point;
using Address = Grid::Address;

void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
Point add(Point a, Point b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Point mul(Point a, double b) { return {a.x*b,a.y*b,a.z*b}; }
double dot(Point a, Point b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
double angle(Point a, Point b) {
    const Point c{a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
    return std::atan2(std::hypot(c.x,c.y,c.z),dot(a,b));
}

void testCanonicalAddresses() {
    for (int n : {1,2,3,4,8,16,64}) {
        const Grid grid(n);
        std::vector<bool> seen(std::size_t(grid.cellCount()), false);
        for (int f=0;f<20;++f) for (int i=0;i<=n;++i) for (int j=0;j<=n-i;++j) {
            const Address a{f,i,j};
            const auto id=grid.id(a);
            require(id<grid.cellCount(),"ID exceeds cell count");
            seen[std::size_t(id)]=true;
            const auto canonical=grid.canonical(a);
            require(grid.id(canonical)==id,"seam canonicalization changed the ID");
            require(grid.address(id)==canonical,"ID decoding did not return canonical address");
        }
        require(std::all_of(seen.begin(),seen.end(),[](bool value){return value;}),"IDs contain holes");
        for (std::uint64_t id=0;id<grid.cellCount();++id)
            require(grid.id(grid.address(id))==id,"ID round trip failed");
    }
}

std::vector<Point> allPoints(const Grid& grid) {
    std::vector<Point> result;
    result.reserve(std::size_t(grid.cellCount()));
    for (std::uint64_t id=0;id<grid.cellCount();++id) result.push_back(grid.point(grid.address(id)));
    return result;
}

void compareWithAngularOracle(const Grid& grid, const std::vector<Point>& sites, Point direction) {
    const Point q=Grid::normalized(direction);
    const Point selected=grid.point(grid.locate(q));
    double expected=4.0;
    // Independent full angular-distance oracle; no face or lattice addressing.
    for (Point site:sites) expected=std::min(expected,angle(q,site));
    const double actual=angle(q,selected);
    require(actual<=expected+2e-12,"direct cell rule differs from spherical nearest site");
}

void testNearestSitesAndBoundaries() {
    std::mt19937_64 rng(0x5F3A8194ULL);
    std::normal_distribution<double> random;
    std::size_t queryCount=0;
    for (int n:{1,2,3,4,8,16,32,64}) {
        const Grid grid(n);
        const auto sites=allPoints(grid);
        const int count=n<=16?1600:240;
        for (int i=0;i<count;++i) {
            compareWithAngularOracle(grid,sites,{random(rng),random(rng),random(rng)});
            ++queryCount;
        }
        for (int i=0;i<96;++i) {
            const auto a=grid.address(rng()%grid.cellCount());
            require(grid.id(grid.locate(grid.point(a)))==grid.id(a),"generator does not locate to itself");
            const auto near=grid.neighbors(a);
            const auto b=near.addresses[rng()%near.count];
            const Point mid=grid.edgeMidpoint(a,b), normal=grid.bisectorNormal(a,b);
            compareWithAngularOracle(grid,sites,mid);
            const double epsilon=1e-7/n;
            require(grid.id(grid.locate(add(mid,mul(normal,epsilon))))==grid.id(a),"wrong side of spherical bisector");
            require(grid.id(grid.locate(add(mid,mul(normal,-epsilon))))==grid.id(b),"wrong opposite side of spherical bisector");
            queryCount+=3;
        }
    }
    std::printf("[IcosahedralGrid] nearest-site and bisector queries=%zu passed\n",queryCount);
}

void testDerivedVoronoiCells() {
    for (int n:{1,2,3,4,8}) {
        const Grid grid(n);
        const auto sites=allPoints(grid);
        int pentagons=0;
        for (std::uint64_t id=0;id<grid.cellCount();++id) {
            const auto a=grid.address(id);
            const auto cell=grid.cell(a);
            require(cell.degree==5 || cell.degree==6,"invalid dual cell degree");
            pentagons+=cell.degree==5;
            for (int k=0;k<cell.degree;++k) {
                const auto neighbor=cell.neighbors[k];
                const auto adjacent=grid.neighbors(neighbor);
                require(std::any_of(adjacent.addresses.begin(),adjacent.addresses.begin()+adjacent.count,
                    [&](Address address){return grid.id(address)==id;}),"grid adjacency is not reciprocal");
                const auto normal=grid.bisectorNormal(a,neighbor);
                const auto v0=cell.vertices[k],v1=cell.vertices[(k+1)%cell.degree];
                require(std::abs(dot(v0,normal))<2e-12 && std::abs(dot(v1,normal))<2e-12,
                    "dual edge is not a spherical equidistance bisector");
                const double radius=angle(v0,cell.generator);
                for (Point site:sites)
                    require(angle(v0,site)>=radius-3e-12,"derived dual corner violates the empty spherical cap");
            }
        }
        require(pentagons==12,"global grid must have exactly twelve pentagons");
    }
}

void testLargeFrequenciesAndDirections() {
    std::mt19937 rng(51234);
    for (int n:{127,1024,65535,Grid::kMaxFrequency}) {
        const Grid grid(n);
        for (int k=0;k<256;++k) {
            const int i=int(rng()%std::uint32_t(n+1));
            const int j=int(rng()%std::uint32_t(n-i+1));
            const Address a{int(rng()%20),i,j};
            const auto id=grid.id(a);
            require(grid.id(grid.address(id))==id,"large-frequency ID round trip failed");
            const Point point=grid.point(a);
            require(grid.id(grid.locate(point))==id,"large-frequency generator lookup failed");
            require(grid.id(grid.locate(mul(point,1e300)))==id,"large direction normalization failed");
            require(grid.id(grid.locate(mul(point,1e-300)))==id,"small direction normalization failed");
        }
    }
    const auto subnormal=Grid::normalized({1e-320,0,0});
    require(subnormal.x==1.0,"subnormal direction normalization failed");
}

void testRayAndInvalidInputs() {
    const Grid grid(16);
    const auto front=grid.locateRay({0,0,3},{0,0,-7});
    require(front && grid.id(*front)==grid.id(grid.locate({0,0,1})),"front ray hit is wrong");
    require(!grid.locateRay({0,0,3},{0,0,1}),"backward ray must miss");
    require(!grid.locateRay({3,0,3},{0,0,-1}),"disjoint ray must miss");
    const auto inside=grid.locateRay({0,0,0},{1,2,3},2.0);
    require(inside && grid.id(*inside)==grid.id(grid.locate({1,2,3})),"inside ray exit is wrong");
    const auto tangent=grid.locateRay({1,0,3},{0,0,-1});
    require(tangent && grid.id(*tangent)==grid.id(grid.locate({1,0,0})),"tangent ray hit is wrong");
    int rejected=0;
    try { Grid invalid(0); } catch (const std::invalid_argument&) { ++rejected; }
    try { (void)grid.locate({}); } catch (const std::invalid_argument&) { ++rejected; }
    try { (void)grid.locate({std::numeric_limits<double>::infinity(),0,0}); } catch (const std::invalid_argument&) { ++rejected; }
    try { (void)grid.id({20,0,0}); } catch (const std::invalid_argument&) { ++rejected; }
    try { (void)grid.address(grid.cellCount()); } catch (const std::out_of_range&) { ++rejected; }
    try { (void)grid.locateRay({},{1,0,0},-1); } catch (const std::invalid_argument&) { ++rejected; }
    require(rejected==6,"invalid grid inputs were not rejected");
}
} // namespace

void runIcosahedralGridUnitTests() {
    testCanonicalAddresses();
    testNearestSitesAndBoundaries();
    testDerivedVoronoiCells();
    testLargeFrequenciesAndDirections();
    testRayAndInvalidInputs();
    std::puts("[IcosahedralGrid] all tests passed");
}

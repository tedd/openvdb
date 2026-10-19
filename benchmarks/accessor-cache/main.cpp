// Copyright Contributors to the OpenVDB Project
// SPDX-License-Identifier: Apache-2.0

#include <openvdb/tree/Tree.h>
#include <openvdb/tree/ValueAccessor.h>
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(BENCH_float)
using Value = float;
#elif defined(BENCH_double)
using Value = double;
#elif defined(BENCH_int64)
using Value = int64_t;
#else
using Value = uint16_t;
#endif
using Tree = openvdb::tree::Tree4<Value, 5, 4, 3>::Type;
using Accessor = openvdb::tree::ValueAccessor<Tree>;
using Clock = std::chrono::steady_clock;

void require(bool result) { if (!result) throw std::runtime_error("Correctness check failed"); }

// Tree::clear and stealNode exercise automatic invalidation. Direct addTile
// requires clearing registered accessors explicitly before replacing nodes.
void correctness()
{
    Tree tree(Value(0));
    Accessor a(tree);
    const openvdb::Coord first(-16, -8, -1), collision(16, -8, -1);
    a.setValueOn(first, 7); a.setValueOn(collision, 9);
    for (int i = 0; i < 100; ++i) {
        require(a.getValue(first) == 7 && a.getValue(collision) == 9);
    }
    Accessor copy(a);
    Tree other(Value(3));
    Accessor assigned(other);
    assigned = a;
    tree.clearAllAccessors();
    tree.addTile(1, first, Value(12), false);
    for (auto p : {&a, &copy, &assigned}) {
        Value value;
        require(!p->probeValue(first, value) && value == 12);
        require(p->getValue(collision) == 9);
    }
    for (auto p : {&a, &copy, &assigned}) {
        p->setValueOn(first, 14);
        require(p->getValue(first) == 14);
    }
    a.clear(); a.template eraseNode<Tree::LeafNodeType>();
    require(a.getValue(first) == 14);
    tree.clear();
    require(a.getValue(first) == 0 && copy.getValue(first) == 0 && assigned.getValue(first) == 0);
    // Clear a bit after saturating the mask word. The next write must reactivate it.
    for (int z = 0; z < 8; ++z) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x)
        a.setValueOn(openvdb::Coord(x, y, z), 5);
    a.setValueOff(openvdb::Coord(1, 2, 3), 17);
    a.setValueOn(openvdb::Coord(1, 2, 3), 19);
    Value value;
    require(a.probeValue(openvdb::Coord(1, 2, 3), value) && value == 19);
    // Tree destruction releases all registered accessor cache entries.
    auto dyingTree = new Tree(Value(0));
    auto survivor = new Accessor(*dyingTree);
    survivor->setValueOn(first, 4);
    delete dyingTree;
    require(survivor->getTree() == nullptr);
    delete survivor;
}

__declspec(noinline) uint64_t read(Accessor& a, const std::vector<openvdb::Coord>& coords, int count)
{
    uint64_t sum = 0;
    const size_t mask = coords.size() - 1;
    for (int i = 0; i < count; ++i) sum += a.getValue(coords[size_t(i) & mask]);
    return sum;
}

__declspec(noinline) void write(Accessor& a, const std::vector<openvdb::Coord>& coords, int count, int phase, bool partial)
{
    const size_t mask = coords.size() - 1;
    if (partial) {
        for (int i = 0; i < count; ++i) {
            const size_t index = (size_t(i) << 1) & mask;
            a.setValueOn(coords[index], Value(1 + ((index + phase) & 7)));
        }
    } else {
        for (int i = 0; i < count; ++i)
            a.setValueOn(coords[size_t(i) & mask], Value(1 + ((i + phase) & 7)));
    }
}

__declspec(noinline) void churn(Tree& tree, Accessor& accessor, int count)
{
    const openvdb::Coord point(1,2,3);
    for (int i=0; i<count; ++i) {
        // stealNode clears all registered accessors before detaching a node.
        // Deleting the returned node and rematerializing are part of the cost.
        delete tree.template stealNode<Tree::LeafNodeType>(point,Value(0),false);
        accessor.setValueOn(point,Value(i));
    }
}

void validate(Tree& tree, const std::vector<openvdb::Coord>& coords, int phase, bool partial)
{
    // Independent tree lookup bypasses the accessor under test.
    uint64_t activeCount = 0;
    for (size_t i = 0; i < coords.size(); ++i) {
        Value actual;
        const bool expectedActive = !partial || (i & 1) == 0;
        const Value expected = Value(1 + ((i + (expectedActive ? phase : 0)) & 7));
        require(tree.probeValue(coords[i], actual) == expectedActive && actual == expected);
        activeCount += expectedActive;
    }
    require(tree.activeVoxelCount() == activeCount);
}

int main(int argc, char** argv) try
{
    auto option = [&](const std::string& key, std::string fallback) {
        for (int i = 1; i + 1 < argc; ++i) if (argv[i] == key) return std::string(argv[i + 1]);
        return fallback;
    };
    const auto pattern = option("--pattern", "coherent64");
    const int operations = std::stoi(option("--operations", "16777216"));
    const int samples = std::stoi(option("--samples", "7"));
    const int warmup = std::stoi(option("--warmup-ms", "500"));
    const int cpu = std::stoi(option("--cpu", "8"));
    const bool writeFirst = option("--write-first", "0") == "1";
    require(cpu >= 0 && cpu < 63 && samples > 0 && warmup >= 0);
    require(SetProcessAffinityMask(GetCurrentProcess(), DWORD_PTR(1ULL << cpu)) != 0);
    correctness();
    if (pattern == "validate") { std::cout << "{\"correctness\":true,\"accessorBytes\":" << sizeof(Accessor) << "}\n"; return 0; }
    if (pattern == "churn") {
        require(operations>0);
        Tree tree(Value(0)); Accessor a(tree);
        const auto warmStart=Clock::now();
        do { churn(tree,a,8192); }
        while (std::chrono::duration<double,std::milli>(Clock::now()-warmStart).count()<warmup);
        std::cout << std::setprecision(17) << "{\"pattern\":\"churn\",\"coordinates\":1,\"coordinateHash\":0,\"activeVoxelCount\":1,\"leafCount\":1,\"accessorBytes\":"
            << sizeof(Accessor) << ",\"msvc\":" << _MSC_FULL_VER << ",\"results\":[{\"name\":\"write\",\"operations\":" << operations << ",\"elapsedMilliseconds\":[";
        for (int sample=0; sample<samples; ++sample) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start=Clock::now(); churn(tree,a,operations); const auto end=Clock::now();
            std::atomic_signal_fence(std::memory_order_seq_cst);
            require(tree.activeVoxelCount()==1 && tree.leafCount()==1);
            for (int x=0;x<8;++x) for (int y=0;y<8;++y) for (int z=0;z<8;++z) {
                Value value; const bool target=x==1 && y==2 && z==3;
                require(tree.probeValue(openvdb::Coord(x,y,z),value)==target && value==(target?Value(operations-1):0));
            }
            if (sample) std::cout << ',';
            std::cout << std::chrono::duration<double,std::milli>(end-start).count();
        }
        std::cout << "]}],\"sink\":" << tree.getValue(openvdb::Coord(1,2,3)) << "}\n";
        return 0;
    }
    const bool single = pattern == "singleleaf";
    const bool rootSpread = pattern == "root64";
    const bool larger = pattern == "coherent512" || pattern == "random512";
    const bool partial = pattern == "partial64";
    const bool collision = pattern == "collision64";
    const bool random = pattern == "random64" || pattern == "random512" || collision;
    require(single || rootSpread || larger || partial || collision || pattern == "coherent64" || pattern == "random64");
    const size_t size = single ? 512 : larger ? 262144 : 32768;
    require(operations >= int(size) && (operations % int(size)) == 0);
    std::vector<openvdb::Coord> coords(size);
    for (size_t i = 0; i < size; ++i) {
        if (single) coords[i] = openvdb::Coord(int(i >> 6), int((i >> 3) & 7), int(i & 7));
        else if (rootSpread) coords[i] = openvdb::Coord((int(i >> 9)-32)*4096+int((i >> 6)&7),int((i >> 3)&7),int(i&7));
        else if (collision) coords[i] = openvdb::Coord((int(i >> 9) - 32) * 32 + int((i >> 6) & 7), int((i >> 3) & 7), int(i & 7));
        else if (larger) coords[i] = openvdb::Coord(int(i >> 12) - 32, int((i >> 6) & 63) - 32, int(i & 63) - 32);
        else coords[i] = openvdb::Coord(int(i >> 10) - 16, int((i >> 5) & 31) - 16, int(i & 31) - 16);
    }
    if (random) {
        uint32_t seed = 7391;
        for (size_t i = size - 1; i > 0; --i) {
            seed = seed * 1664525U + 1013904223U;
            std::swap(coords[i], coords[seed % (i + 1)]);
        }
    }
    Tree tree(Value(0));
    Accessor a(tree);
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < size; ++i) {
        if (partial && (i & 1)) a.setValueOff(coords[i], Value(1 + (i & 7)));
        else a.setValueOn(coords[i], Value(1 + (i & 7)));
        for (auto v : {coords[i].x(), coords[i].y(), coords[i].z()}) hash = (hash ^ uint32_t(v)) * 1099511628211ULL;
    }
    validate(tree, coords, 0, partial);
    uint64_t sink = 0;
    std::cout << std::setprecision(17) << "{\"pattern\":\"" << pattern << "\",\"coordinates\":" << size
        << ",\"coordinateHash\":" << hash << ",\"activeVoxelCount\":" << tree.activeVoxelCount()
        << ",\"leafCount\":" << tree.leafCount() << ",\"accessorBytes\":" << sizeof(Accessor)
        << ",\"msvc\":" << _MSC_FULL_VER << ",\"results\":[";
    bool first = true;
    for (auto workload : writeFirst ? std::vector<std::string>{"write", "read"} : std::vector<std::string>{"read", "write"}) {
        const auto startWarmup = Clock::now();
        do {
            if (workload == "read") sink = read(a, coords, int(size));
            else write(a, coords, int(size), 3, partial);
        } while (std::chrono::duration<double, std::milli>(Clock::now() - startWarmup).count() < warmup);
        write(a, coords, int(size), 0, partial);
        validate(tree, coords, 0, partial);
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"name\":\"" << workload << "\",\"operations\":" << operations << ",\"elapsedMilliseconds\":[";
        for (int sample = 0; sample < samples; ++sample) {
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto start = Clock::now();
            if (workload == "read") sink = read(a, coords, operations);
            else write(a, coords, operations, sample + 1, partial);
            std::atomic_signal_fence(std::memory_order_seq_cst);
            const auto end = Clock::now();
            if (workload == "read") require(sink == uint64_t(operations / 8) * 36);
            validate(tree, coords, workload == "read" ? 0 : sample + 1, partial);
            if (sample) std::cout << ',';
            std::cout << std::chrono::duration<double, std::milli>(end - start).count();
        }
        std::cout << "]}";
        write(a, coords, int(size), 0, partial);
    }
    std::cout << "],\"sink\":" << sink << "}\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }

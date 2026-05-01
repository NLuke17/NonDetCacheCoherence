#include <bits/stdc++.h>
#include <chrono>
using namespace std;
using namespace chrono;

/* ===================== BLOOM FILTER ===================== */

struct BloomFilter {
    int size;
    vector<bool> bits;

    BloomFilter(int s) : size(s), bits(s, false) {}

    int hash1(int x) const { return (x * 31) % size; }
    int hash2(int x) const { return (x * 131) % size; }

    void add(int x) {
        bits[hash1(x)] = true;
        bits[hash2(x)] = true;
    }

    bool possiblyContains(int x) const {
        return bits[hash1(x)] && bits[hash2(x)];
    }

    void clear() {
        fill(bits.begin(), bits.end(), false);
    }
};

/* ===================== CORE CACHE ===================== */

struct Core {
    unordered_map<int, int> cache;

    bool has(int addr) {
        return cache.count(addr);
    }

    void write(int addr, int val) {
        cache[addr] = val;
    }

    void invalidate(int addr) {
        cache.erase(addr);
    }
};

/* ===================== SIMULATOR ===================== */

enum Mode { EXACT, BLOOM };

struct Simulator {
    int numCores, numAddrs;
    Mode mode;
    int bloomSize;

    vector<Core> cores;
    unordered_map<int, int> memory;

    unordered_map<int, set<int>> directory;
    unordered_map<int, BloomFilter> bloomDirectory;

    int hits = 0, misses = 0;
    int invalidations = 0, unnecessaryInvalidations = 0;
    int staleReads = 0;

    long long coherenceCost = 0;

    Simulator(int nCores, int nAddrs, Mode m, int bSize = 64)
        : numCores(nCores), numAddrs(nAddrs), mode(m), bloomSize(bSize),
          cores(nCores) {

        for (int i = 0; i < nAddrs; i++) {
            memory[i] = 0;
            if (mode == BLOOM)
                bloomDirectory.emplace(i, BloomFilter(bloomSize));
        }
    }

    void read(int core, int addr) {
        if (cores[core].has(addr)) {
            hits++;

            if (cores[core].cache[addr] != memory[addr]) {
                staleReads++;
                coherenceCost += 5;
            } else {
                coherenceCost += 1;
            }
            return;
        }

        misses++;
        cores[core].write(addr, memory[addr]);

        coherenceCost += 10; 

        if (mode == EXACT) {
            directory[addr].insert(core);
            coherenceCost += directory[addr].size(); 
        } else {
            bloomDirectory.at(addr).add(core);
            coherenceCost += 2;
        }
    }

    void write(int core, int addr, int val) {
        memory[addr] = val;

        coherenceCost += 20;
        if (mode == EXACT) {

            coherenceCost += directory[addr].size() * 5;

            for (int c : directory[addr]) {
                if (c != core && cores[c].has(addr)) {
                    cores[c].invalidate(addr);
                    invalidations++;
                    coherenceCost += 15;
                }
            }

            directory[addr].clear();
            directory[addr].insert(core);

        } else {
            auto &bf = bloomDirectory.at(addr);

            double skipProb = 1.0 / bloomSize;

            for (int c = 0; c < numCores; c++) {
                if (c == core) continue;

                if (bf.possiblyContains(c)) {

                    coherenceCost += 2;

                    if ((double)rand() / RAND_MAX < skipProb)
                        continue;

                    if (cores[c].has(addr)) {
                        cores[c].invalidate(addr);
                        invalidations++;
                        coherenceCost += 10;
                    } else {
                        unnecessaryInvalidations++;
                        coherenceCost += 1;
                    }
                }
            }

            bf.clear();
            bf.add(core);
        }

        cores[core].write(addr, val);
    }

    void randomStep() {
        int core = rand() % numCores;
        int addr = rand() % numAddrs;

        if (rand() % 2)
            read(core, addr);
        else
            write(core, addr, rand() % 1000);
    }

    void run(int steps) {
        for (int i = 0; i < steps; i++)
            randomStep();
    }

    void printStats() {
        cout << "Hits: " << hits
             << ", Misses: " << misses
             << ", Invalidations: " << invalidations
             << ", Unnecessary: " << unnecessaryInvalidations
             << ", Stale Reads: " << staleReads << "\n";
    }
};

/* ===================== MEMORY ===================== */

long long estimateMemoryExact(const unordered_map<int, set<int>>& directory) {
    long long mem = 0;
    for (auto &p : directory) {
        mem += sizeof(int);
        mem += p.second.size() * sizeof(int);
    }
    return mem;
}

long long estimateMemoryBloom(const unordered_map<int, BloomFilter>& bloom) {
    long long mem = 0;
    for (auto &p : bloom) {
        mem += sizeof(int);
        mem += p.second.size / 8;
    }
    return mem;
}


int main() {
    int cores = 8;
    int addrs = 8;
    int steps = 200000;

    srand(0);


    cout << "=== Deterministic Coherence ===\n";

    Simulator exactSim(cores, addrs, EXACT);

    auto start = high_resolution_clock::now();
    exactSim.run(steps);
    auto end = high_resolution_clock::now();

    auto exactTime = duration_cast<milliseconds>(end - start).count();

    exactSim.printStats();
    cout << "Time (ms): " << exactTime << "\n";
    cout << "Estimated Memory: "
         << estimateMemoryExact(exactSim.directory)
         << " bytes\n";
    cout << "Coherence Cost: " << exactSim.coherenceCost << "\n\n";


    cout << "=== Approximate Coherence (Bloom Sweep) ===\n";

    vector<int> sizes = {4, 8, 16, 32, 64};

    for (int sz : sizes) {
        srand(0);

        Simulator sim(cores, addrs, BLOOM, sz);

        auto start = high_resolution_clock::now();
        sim.run(steps);
        auto end = high_resolution_clock::now();

        auto ms = duration_cast<milliseconds>(end - start).count();

        sim.printStats();

        cout << "Bloom size = " << sz << "\n";
        cout << "Time (ms): " << ms << "\n";
        cout << "Estimated Memory: "
             << estimateMemoryBloom(sim.bloomDirectory)
             << " bytes\n";
        cout << "Coherence Cost: " << sim.coherenceCost << "\n\n";
    }

    return 0;
}
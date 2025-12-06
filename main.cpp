#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <iomanip>
#include <filesystem>

using namespace std;
namespace fs = std::filesystem;

double qpc_diff_sec(const LARGE_INTEGER &t1, const LARGE_INTEGER &t2) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return double(t2.QuadPart - t1.QuadPart) / double(freq.QuadPart);
}

void write_csv_line(ofstream &ofs, const string &line) {
    ofs << line << '\n';
    ofs.flush();
    cout << line << '\n';
}

// compute average
double avg(const vector<double> &v) {
    double s = 0;
    for (double x : v) s += x;
    return s / (v.empty() ? 1.0 : (double)v.size());
}
// population stddev (sqrt(mean square deviation))
double stddev(const vector<double> &v, double mean) {
    if (v.empty()) return 0.0;
    double s = 0;
    for (double x : v) s += (x - mean) * (x - mean);
    return sqrt(s / v.size());
}

// write file in chunks of chunkSize until totalBytes written
bool file_write_chunks(const string &path, const vector<char> &data, size_t chunkSize, double &elapsedSec) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    LARGE_INTEGER t1, t2;
    QueryPerformanceCounter(&t1);

    size_t total = data.size();
    size_t pos = 0;
    while (pos < total) {
        DWORD toWrite = (DWORD)min(chunkSize, total - pos);
        BOOL ok = WriteFile(h, data.data() + pos, toWrite, &written, NULL);
        if (!ok) { CloseHandle(h); return false; }
        pos += written;
    }
    FlushFileBuffers(h); // ensure data on disk
    QueryPerformanceCounter(&t2);
    CloseHandle(h);

    elapsedSec = qpc_diff_sec(t1, t2);
    return true;
}

// read file in chunks of chunkSize until totalBytes read
bool file_read_chunks(const string &path, vector<char> &buf, size_t chunkSize, double &elapsedSec) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    DWORD readn = 0;
    LARGE_INTEGER t1, t2;
    QueryPerformanceCounter(&t1);

    size_t total = buf.size();
    size_t pos = 0;
    while (pos < total) {
        DWORD toRead = (DWORD)min(chunkSize, total - pos);
        BOOL ok = ReadFile(h, buf.data() + pos, toRead, &readn, NULL);
        if (!ok) { CloseHandle(h); return false; }
        pos += readn;
        if (readn == 0) break;
    }

    QueryPerformanceCounter(&t2);
    CloseHandle(h);

    elapsedSec = qpc_diff_sec(t1, t2);
    return true;
}

// run single configuration (memoryType, blockSize, bufferSize, launches)
// outputs multiple CSV lines: one per launch but with averages/stats included
void run_config(ofstream &ofs,
                const string &memoryType,
                size_t blockSize,
                size_t bufferSize,
                int launches,
                const string &timerName,
                const string &outDir) {

    string elementType = "char";
    vector<double> writeTimes;
    vector<double> readTimes;

    // Prepare data buffer of blockSize bytes
    vector<char> data(blockSize);
    for (size_t i = 0; i < blockSize; ++i) data[i] = (char)(i & 0xFF);

    // Ensure output directory exists (for disk types)
    if (!outDir.empty()) fs::create_directories(outDir);

    // filename per config (to avoid collisions)
    string filename = outDir.empty() ? "test_storage.bin" : (outDir + "\\test_storage.bin");

    for (int run = 1; run <= launches; ++run) {
        double wt = 0.0, rt = 0.0;
        bool ok = true;

        if (memoryType == "RAM") {
            // For RAM: do writes/reads in memory using bufferSize as chunk copy size
            LARGE_INTEGER t1, t2;
            // write chunk-wise to simulate buffer-size effect
            QueryPerformanceCounter(&t1);
            size_t pos = 0;
            while (pos < blockSize) {
                size_t chunk = min(bufferSize, blockSize - pos);
                // write chunk: simple memory set/copy
                for (size_t i = 0; i < chunk; ++i) data[pos + i] = (char)((pos + i) & 0xFF);
                pos += chunk;
            }
            QueryPerformanceCounter(&t2);
            wt = qpc_diff_sec(t1, t2);

            volatile char sink;
            QueryPerformanceCounter(&t1);
            pos = 0;
            while (pos < blockSize) {
                size_t chunk = min(bufferSize, blockSize - pos);
                for (size_t i = 0; i < chunk; ++i) sink = data[pos + i];
                pos += chunk;
            }
            QueryPerformanceCounter(&t2);
            rt = qpc_diff_sec(t1, t2);

        } else {
            // For storage types: write/read to file in chunks of bufferSize
            // Create directory earlier; filename includes type to separate devices
            // Write
            ok = file_write_chunks(filename, data, bufferSize, wt);
            // Read into read buffer
            vector<char> rbuf(blockSize);
            if (ok) ok = file_read_chunks(filename, rbuf, bufferSize, rt);
            // remove file to avoid filling disk
            if (fs::exists(filename)) fs::remove(filename);
            if (!ok) {
                // skip this run (record large time or zero)
                wt = 0.0;
                rt = 0.0;
            }
        }

        writeTimes.push_back(wt);
        readTimes.push_back(rt);
    } // end runs

    double avgW = avg(writeTimes);
    double avgR = avg(readTimes);
    double errW = stddev(writeTimes, avgW);
    double errR = stddev(readTimes, avgR);

    // bandwidth: BLOCK_SIZE / AverageTime * 1e-6 -> MB/s (as in spec: *1e6 then [Mb/s], here use MB/s)
    double bwW = (avgW > 0.0) ? (double(blockSize) / avgW) * 1e-6 : 0.0;
    double bwR = (avgR > 0.0) ? (double(blockSize) / avgR) * 1e-6 : 0.0;

    // For each launch produce a CSV line containing that launch's WriteTime but overall averages/stats
    for (int run = 1; run <= launches; ++run) {
        double wt = writeTimes[run - 1];
        double rt = readTimes[run - 1];

        // RelError in percent (guard divide-by-zero)
        double relW = (avgW > 0.0) ? (errW / avgW) * 100.0 : 0.0;
        double relR = (avgR > 0.0) ? (errR / avgR) * 100.0 : 0.0;

        // Compose CSV line
        // MemoryType;BlockSize;ElementType;BufferSize;LaunchNum;Timer;WriteTime;AverageWriteTime;WriteBandwidth;AbsError(write);RelError(write);ReadTime;AverageReadTime;ReadBandwidth;AbsError(read);RelError(read);
        ostringstream ss;
        ss << fixed << setprecision(9);
        ss << memoryType << ";"
           << blockSize << ";"
           << elementType << ";"
           << bufferSize << ";"
           << run << ";"
           << timerName << ";"
           << wt << ";"
           << avgW << ";"
           << bwW << ";"
           << errW << ";"
           << relW << ";"
           << rt << ";"
           << avgR << ";"
           << bwR << ";"
           << errR << ";"
           << relR << ";";

        write_csv_line(ofs, ss.str());
    }
}

int main() {
    // Fixed output CSV name
    const string csvname = "results.csv";
    ofstream ofs(csvname, ios::out | ios::trunc);
    if (!ofs.is_open()) {
        cerr << "Cannot open " << csvname << " for writing\n";
        return 1;
    }

    // CSV header (one-line header)
    ofs << "MemoryType;BlockSize;ElementType;BufferSize;LaunchNum;Timer;WriteTime;AverageWriteTime;WriteBandwidth;AbsError(write);RelError(write);ReadTime;AverageReadTime;ReadBandwidth;AbsError(read);RelError(read);" << '\n';
    cout << "Creating " << csvname << " and starting tests...\n";

    // Timer name
    const string timerName = "QueryPerformanceCounter";

    // ---------------- RAM TESTS ----------------
    // Typical sizes (if you wish, edit values to match your CPU caches)
    vector<pair<string, size_t>> ram_tests = {
            {"RAM_cacheline", 64},            // cache line ~64B
            {"RAM_L1", 32 * 1024},            // L1 ~32KB
            {"RAM_L2", 256 * 1024},           // L2 ~256KB
            {"RAM_L3", 8 * 1024 * 1024},      // L3 ~8MB (typical; edit if needed)
            {"RAM_big", 64 * 1024 * 1024}     // > L3, e.g., 64MB
    };

    // For RAM use a few launches (faster)
    int ram_launches = 10;
    // For RAM BufferSizes to probe effect (use chunk copy sizes)
    vector<size_t> ram_buffer_sizes = {64, 4096, 65536, 1024 * 1024}; // 64B, 4KB, 64KB, 1MB

    for (auto &rt : ram_tests) {
        for (size_t bsize : ram_buffer_sizes) {
            // run_config writes launches lines per config
            run_config(ofs, "RAM", rt.second, bsize, ram_launches, timerName, "");
        }
    }

    // ---------------- STORAGE TESTS (HDD/SSD/flash) ----------------
    // We will run 20 tests starting from 4MB with step 4MB: i.e. 4MB,8MB,...,80MB
    int disk_tests_count = 20;
    size_t disk_start_mb = 4;
    size_t disk_step_mb = 4;

    vector<string> storage_types = {"HDD", "SSD", "flash"};
    // Buffer sizes to test influence of BufferSize on throughput
    vector<size_t> disk_buffer_sizes = {4096, 65536, 1024 * 1024}; // 4KB, 64KB, 1MB

    // For storage runs use 20 launches as requested in task
    int disk_launches = 20;

    // Create directories to write files (one per storage type) ? user should ensure correct mount (here it's just folders)
    for (const auto &stype : storage_types) {
        string dir = stype + "_test_files";
        fs::create_directories(dir);
        for (int t = 0; t < disk_tests_count; ++t) {
            size_t block_mb = disk_start_mb + (size_t)t * disk_step_mb;
            size_t blockSize = block_mb * 1024 * 1024ULL;
            for (size_t bufSize : disk_buffer_sizes) {
                run_config(ofs, stype, blockSize, bufSize, disk_launches, timerName, dir);
            }
        }
    }

    ofs.close();
    cout << "All tests finished. Results saved to " << csvname << "\n";
    return 0;
}
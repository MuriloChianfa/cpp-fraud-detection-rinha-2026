#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>
#include <zlib.h>

#include <simdjson.h>

namespace {

constexpr int    DIM    = 14;
constexpr int    STRIDE = 16;
constexpr float  S      = 8192.0f;
constexpr int    S_I    = 8192;
constexpr size_t EXPECTED_N = 100000;

std::vector<char> gunzip_file(const std::string& path) {
    gzFile gz = gzopen(path.c_str(), "rb");
    if (!gz) {
        std::fprintf(stderr, "[prepare_refs] gzopen %s failed\n", path.c_str());
        std::exit(1);
    }
    std::vector<char> out;
    out.reserve(12 * 1024 * 1024);
    constexpr size_t BUF = 64 * 1024;
    std::array<char, BUF> tmp{};
    for (;;) {
        int n = gzread(gz, tmp.data(), tmp.size());
        if (n < 0) {
            int errnum = 0;
            std::fprintf(stderr, "[prepare_refs] gzread error: %s\n", gzerror(gz, &errnum));
            gzclose(gz);
            std::exit(1);
        }
        if (n == 0) break;
        out.insert(out.end(), tmp.begin(), tmp.begin() + n);
    }
    gzclose(gz);
    return out;
}

inline int16_t quant(double v, int dim) {
    if ((dim == 5 || dim == 6) && v < 0.0) return static_cast<int16_t>(-S_I);
    if (dim == 9 || dim == 10 || dim == 11) {
        return v > 0.5 ? static_cast<int16_t>(S_I) : int16_t{0};
    }
    double c = v;
    if (c < 0.0) c = 0.0;
    else if (c > 1.0) c = 1.0;
    long q = static_cast<long>(std::lround(c * static_cast<double>(S)));
    if (q > 32767) q = 32767;
    else if (q < -32768) q = -32768;
    return static_cast<int16_t>(q);
}

inline uint64_t l2_sq(const std::array<int16_t, STRIDE>& v) {
    uint64_t s = 0;
    for (int i = 0; i < DIM; ++i) {
        long x = v[i];
        s += static_cast<uint64_t>(x * x);
    }
    return s;
}

}

int main(int argc, char** argv) {
    if (argc != 5) {
        std::fprintf(stderr,
            "usage: %s <references.json.gz> <out_refs.bin> <out_labels.bin> <out_norms.bin>\n",
            argv[0]);
        return 2;
    }
    const std::string in_gz   = argv[1];
    const std::string out_ref = argv[2];
    const std::string out_lab = argv[3];
    const std::string out_nrm = argv[4];

    std::fprintf(stderr, "[prepare_refs] inflating %s\n", in_gz.c_str());
    auto json_buf = gunzip_file(in_gz);
    std::fprintf(stderr, "[prepare_refs] inflated bytes: %zu\n", json_buf.size());

    simdjson::dom::parser parser;
    simdjson::padded_string padded(json_buf.data(), json_buf.size());
    json_buf.clear();
    json_buf.shrink_to_fit();

    auto root = parser.parse(padded);
    if (root.error()) {
        std::fprintf(stderr, "[prepare_refs] simdjson parse failed: %s\n",
                     simdjson::error_message(root.error()));
        return 1;
    }
    simdjson::dom::array arr = root.value();
    const size_t n = arr.size();
    if (n != EXPECTED_N) {
        std::fprintf(stderr, "[prepare_refs] expected %zu refs, got %zu\n",
                     EXPECTED_N, n);
        return 1;
    }

    struct Record {
        std::array<int16_t, STRIDE> v{};
        uint8_t                     label{};
        uint64_t                    norm_sq{};
    };
    std::vector<Record> recs;
    recs.reserve(n);

    size_t idx = 0;
    for (auto entry : arr) {
        auto vec_field = entry["vector"];
        if (vec_field.error()) {
            std::fprintf(stderr, "[prepare_refs] entry %zu missing 'vector'\n", idx);
            return 1;
        }
        simdjson::dom::array vec = vec_field.value();
        if (vec.size() != DIM) {
            std::fprintf(stderr, "[prepare_refs] entry %zu has %zu dims (want %d)\n",
                         idx, vec.size(), DIM);
            return 1;
        }

        Record r;
        int dim = 0;
        for (auto e : vec) {
            double f;
            if (e.is_double())      f = double(e);
            else if (e.is_int64())  f = static_cast<double>(int64_t(e));
            else if (e.is_uint64()) f = static_cast<double>(uint64_t(e));
            else {
                std::fprintf(stderr, "[prepare_refs] entry %zu dim %d non-numeric\n",
                             idx, dim);
                return 1;
            }
            r.v[dim] = quant(f, dim);
            ++dim;
        }
        std::string_view label = entry["label"];
        r.label    = (label == "fraud") ? 1u : 0u;
        r.norm_sq  = l2_sq(r.v);

        recs.push_back(std::move(r));
        ++idx;
    }

    std::sort(recs.begin(), recs.end(),
              [](const Record& a, const Record& b){ return a.norm_sq < b.norm_sq; });

    {
        std::ofstream f(out_ref, std::ios::binary);
        if (!f) { std::fprintf(stderr, "[prepare_refs] cannot open %s\n", out_ref.c_str()); return 1; }
        for (const auto& r : recs) {
            f.write(reinterpret_cast<const char*>(r.v.data()),
                    sizeof(int16_t) * STRIDE);
        }
        f.flush();
        if (!f) { std::fprintf(stderr, "[prepare_refs] write %s failed\n", out_ref.c_str()); return 1; }
    }

    {
        std::ofstream f(out_lab, std::ios::binary);
        if (!f) { std::fprintf(stderr, "[prepare_refs] cannot open %s\n", out_lab.c_str()); return 1; }
        for (const auto& r : recs) {
            f.put(static_cast<char>(r.label));
        }
        f.flush();
        if (!f) { std::fprintf(stderr, "[prepare_refs] write %s failed\n", out_lab.c_str()); return 1; }
    }

    {
        std::ofstream f(out_nrm, std::ios::binary);
        if (!f) { std::fprintf(stderr, "[prepare_refs] cannot open %s\n", out_nrm.c_str()); return 1; }
        for (const auto& r : recs) {
            double n = std::sqrt(static_cast<double>(r.norm_sq));
            long ln = static_cast<long>(std::floor(n));
            if (ln < 0) ln = 0;
            else if (ln > 65535) ln = 65535;
            uint16_t nrm = static_cast<uint16_t>(ln);
            f.write(reinterpret_cast<const char*>(&nrm), sizeof(uint16_t));
        }
        f.flush();
        if (!f) { std::fprintf(stderr, "[prepare_refs] write %s failed\n", out_nrm.c_str()); return 1; }
    }

    size_t fraud_total = 0;
    uint64_t max_norm_sq = 0;
    for (const auto& r : recs) {
        fraud_total += r.label;
        if (r.norm_sq > max_norm_sq) max_norm_sq = r.norm_sq;
    }
    std::fprintf(stderr,
        "[prepare_refs] wrote %zu refs (%.1f MB) + %zu labels (%zu fraud) + %zu norms (max |r| ~= %.0f)\n",
        recs.size(),
        double(recs.size() * STRIDE * sizeof(int16_t)) / (1024.0 * 1024.0),
        recs.size(),
        fraud_total,
        recs.size(),
        std::sqrt(static_cast<double>(max_norm_sq)));
    return 0;
}

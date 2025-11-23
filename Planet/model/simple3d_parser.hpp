// simple3d_parser.hpp — header‑only minimal parsers for OBJ (text) and STL (binary)
// C++20, no external deps. Public domain / CC0.
#pragma once
#include <vector>
#include <string>
#include <string_view>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <optional>
#include <unordered_map>
#include <array>
#include <cmath>

namespace simple3d {

    struct Mesh {
        // Geometry
        std::vector<float> positions; // x,y,z triplets
        std::vector<float> normals;   // x,y,z triplets (can be empty if not present)
        std::vector<float> texcoords; // u,v pairs (optional)
        std::vector<uint32_t> indices; // triangle indices (0‑based)

        // Grouping / materials (minimal): faceMaterial[i] gives material index for triangle i, or UINT32_MAX
        std::vector<uint32_t> faceMaterial;
        std::vector<std::string> materialNames; // index -> name (no .mtl parsing here)

        void clear() {
            positions.clear(); normals.clear(); texcoords.clear(); indices.clear(); faceMaterial.clear(); materialNames.clear();
        }

        size_t vertexCount() const { return positions.size() / 3; }
        size_t triangleCount() const { return indices.size() / 3; }
    };

    // ======= utilities =======
    namespace detail {
        inline void ltrim(std::string_view& sv) {
            size_t i = 0; while (i < sv.size() && (sv[i] == ' ' || sv[i] == '\t' || sv[i] == '\r')) ++i; sv.remove_prefix(i);
        }
        inline void rtrim(std::string_view& sv) {
            size_t n = sv.size(); while (n > 0 && (sv[n - 1] == ' ' || sv[n - 1] == '\t' || sv[n - 1] == '\r' || sv[n - 1] == '\n')) --n; sv = sv.substr(0, n);
        }
        inline void trim(std::string_view& sv) { ltrim(sv); rtrim(sv); }

        inline bool from_chars_float(std::string_view sv, float& out) {
            // std::from_chars for float is C++17 but not fully implemented everywhere; fallback via strtof if needed
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611 && !defined(__GLIBCXX__) // libstdc++ historically lacked float overload
            const char* first = sv.data(); const char* last = first + sv.size();
            auto res = std::from_chars(first, last, out);
            return res.ec == std::errc{};
#else
            std::string tmp(sv);
            char* endp = nullptr; out = std::strtof(tmp.c_str(), &endp);
            return endp && *endp == '\0';
#endif
        }

        inline bool from_chars_int(std::string_view sv, int& out) {
            const char* first = sv.data(); const char* last = first + sv.size();
            auto res = std::from_chars(first, last, out);
            return res.ec == std::errc{};
        }

        inline std::vector<std::string_view> split_spaces(std::string_view sv) {
            std::vector<std::string_view> tok; ltrim(sv);
            size_t i = 0, n = sv.size();
            while (i < n) {
                size_t j = i; while (j < n && sv[j] != ' ' && sv[j] != '\t') ++j;
                tok.emplace_back(sv.substr(i, j - i));
                i = j; while (i < n && (sv[i] == ' ' || sv[i] == '\t')) ++i;
            }
            return tok;
        }

        struct Idx { int v = -0x7fffffff, vt = INT32_MIN, vn = INT32_MIN; };




        inline bool parse_v_vt_vn(std::string_view sv, Idx& out) {
            // formats: v, v/vt, v//vn, v/vt/vn (OBJ 1-based, negatives allowed)
            // we split by '/'
            int slash1 = -1, slash2 = -1; for (size_t i = 0;i < sv.size();++i) { if (sv[i] == '/') { if (slash1 < 0) slash1 = (int)i; else { slash2 = (int)i; break; } } }
            auto parse_int = [](std::string_view s, int& x) { return detail::from_chars_int(s, x); };
            if (slash1 < 0) { return parse_int(sv, out.v); }
            if (slash2 < 0) {
                if (!parse_int(sv.substr(0, slash1), out.v)) return false;
                std::string_view b = sv.substr(slash1 + 1);
                if (b.size()) parse_int(b, out.vt);
                return true;
            }
            if (!parse_int(sv.substr(0, slash1), out.v)) return false;
            if (slash2 == slash1 + 1) {
                // v//vn
                std::string_view tail = sv.substr(slash2 + 1);
                if (tail.size()) parse_int(tail, out.vn);
                return true;
            }
            else {
                std::string_view vt = sv.substr(slash1 + 1, slash2 - slash1 - 1);
                if (vt.size()) parse_int(vt, out.vt);
                std::string_view vn = sv.substr(slash2 + 1);
                if (vn.size()) parse_int(vn, out.vn);
                return true;
            }
        }

        inline uint32_t wrap_index(int idx, uint32_t count) {
            // OBJ: positive are 1-based, negative are relative to end
            if (idx > 0) return (uint32_t)(idx - 1);
            if (idx < 0) return (uint32_t)((int)count + idx);
            return 0; // undefined, but keep safe
        }

        inline std::array<float, 3> tri_normal(const float* a, const float* b, const float* c) {
            float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
            float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
            float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
            float len = std::sqrt(nx * nx + ny * ny + nz * nz); if (len > 0) { nx /= len; ny /= len; nz /= len; }
            return { nx,ny,nz };
        }
    }

    struct ObjOptions {
        bool triangulate_quads = true;  // fan‑triangulate n‑gons
        bool compute_missing_normals = true; // per‑vertex smoothing if no vn provided
    };

    inline bool load_obj(std::istream& in, Mesh& mesh, const ObjOptions& opt = {}) {
        mesh.clear();
        std::vector<float> tmp_pos; tmp_pos.reserve(1 << 16);
        std::vector<float> tmp_nrm; tmp_nrm.reserve(1 << 15);
        std::vector<float> tmp_uv;  tmp_uv.reserve(1 << 15);

        // We build final vertices by indexing directly: for simplicity we only support positions indexed alone (common case). If vt/vn present but not matching, we expand later.
        struct Corner { uint32_t v{}, vt{ UINT32_MAX }, vn{ UINT32_MAX }; };

        std::string curMtl;
        std::unordered_map<std::string, uint32_t> mtlIndex; // name->idx

        std::string line; line.reserve(256);
        std::vector<Corner> corners;



        while (std::getline(in, line)) {
            std::string_view sv(line);
            detail::trim(sv);
            if (sv.empty() || sv[0] == '#') continue;
            if (sv.size() >= 2 && sv[0] == 'v' && sv[1] == ' ') {
                // v x y z [w]
                auto toks = detail::split_spaces(sv.substr(2));
                if (toks.size() < 3) continue;
                float x = 0, y = 0, z = 0; detail::from_chars_float(toks[0], x); detail::from_chars_float(toks[1], y); detail::from_chars_float(toks[2], z);
                tmp_pos.insert(tmp_pos.end(), { x,y,z });
                continue;
            }
            if (sv.size() >= 3 && sv[0] == 'v' && sv[1] == 't' && sv[2] == ' ') {
                auto toks = detail::split_spaces(sv.substr(3));
                if (toks.size() < 2) continue; float u = 0, v = 0; detail::from_chars_float(toks[0], u); detail::from_chars_float(toks[1], v);
                tmp_uv.insert(tmp_uv.end(), { u,v });
                continue;
            }
            if (sv.size() >= 3 && sv[0] == 'v' && sv[1] == 'n' && sv[2] == ' ') {
                auto toks = detail::split_spaces(sv.substr(3));
                if (toks.size() < 3) continue; float x = 0, y = 0, z = 0; detail::from_chars_float(toks[0], x); detail::from_chars_float(toks[1], y); detail::from_chars_float(toks[2], z);
                tmp_nrm.insert(tmp_nrm.end(), { x,y,z });
                continue;
            }
            if (sv.size() >= 2 && sv[0] == 'f' && sv[1] == ' ') {
                auto toks = detail::split_spaces(sv.substr(2));
                if (toks.size() < 3) continue;
                corners.clear(); corners.resize(toks.size());
                // parse all corners
                for (size_t i = 0;i < toks.size();++i) {
                    detail::Idx id; detail::parse_v_vt_vn(toks[i], id);
                    uint32_t v = detail::wrap_index(id.v, (uint32_t)(tmp_pos.size() / 3));
                    uint32_t vt = (id.vt == INT32_MIN) ? UINT32_MAX : detail::wrap_index(id.vt, (uint32_t)(tmp_uv.size() / 2));
                    uint32_t vn = (id.vn == INT32_MIN) ? UINT32_MAX : detail::wrap_index(id.vn, (uint32_t)(tmp_nrm.size() / 3));
                    corners[i] = Corner{ v,vt,vn };
                }
                // triangulate via fan
                const size_t n = corners.size();
                if (n >= 3) {
                    uint32_t mtl = UINT32_MAX;
                    if (!curMtl.empty()) {
                        auto it = mtlIndex.find(curMtl);
                        if (it == mtlIndex.end()) { mtl = (uint32_t)mesh.materialNames.size(); mtlIndex[curMtl] = mtl; mesh.materialNames.push_back(curMtl); }
                        else mtl = it->second;
                    }
                    for (size_t k = 1; k + 1 < n; ++k) {
                        const Corner& a = corners[0];
                        const Corner& b = corners[k];
                        const Corner& c = corners[k + 1];
                        mesh.indices.push_back(a.v);
                        mesh.indices.push_back(b.v);
                        mesh.indices.push_back(c.v);
                        mesh.faceMaterial.push_back(mtl);
                    }
                }
                continue;
            }
            if (sv.rfind("usemtl ", 0) == 0) { std::string name(sv.substr(7)); detail::trim((std::string_view&)sv); curMtl = name; continue; }
            // ignore: mtllib, o, g, s, etc.
        }

        // move positions/uvs as-is
        mesh.positions = std::move(tmp_pos);

        // Handle normals: if any vn indices existed, we ignore per-corner mapping and will compute smoothed unless vn available exactly per vertex id.
        bool has_vn = !tmp_nrm.empty();
        if (has_vn) {
            // Naive: if count matches vertices, keep; else compute smooth
            if (tmp_nrm.size() / 3 == mesh.positions.size() / 3) mesh.normals = std::move(tmp_nrm);
        }



        if (mesh.normals.empty() && opt.compute_missing_normals && !mesh.indices.empty()) {
            mesh.normals.assign(mesh.positions.size(), 0.0f);
            auto V = [&](uint32_t i) { return &mesh.positions[i * 3]; };
            for (size_t t = 0; t < mesh.indices.size(); t += 3) {
                uint32_t ia = mesh.indices[t], ib = mesh.indices[t + 1], ic = mesh.indices[t + 2];
                auto nrm = detail::tri_normal(V(ia), V(ib), V(ic));
                for (uint32_t idx : {ia, ib, ic}) {
                    mesh.normals[idx * 3 + 0] += nrm[0];
                    mesh.normals[idx * 3 + 1] += nrm[1];
                    mesh.normals[idx * 3 + 2] += nrm[2];
                }
            }
            for (size_t i = 0;i < mesh.positions.size(); i += 3) {
                float nx = mesh.normals[i], ny = mesh.normals[i + 1], nz = mesh.normals[i + 2];
                float len = std::sqrt(nx * nx + ny * ny + nz * nz); if (len > 0) { mesh.normals[i] = nx / len; mesh.normals[i + 1] = ny / len; mesh.normals[i + 2] = nz / len; }
            }
        }

        // Note: We do not expand vertices to match vt/vn per-corner. Simple renderers can use positions+indices, and sample UVs per vertex if authored that way.
        // Advanced: implement vertex dedup with (v,vt,vn) key — omitted for minimalism.

        return !mesh.positions.empty();
    }

    // Convenience overload
    inline bool load_obj_file(const std::string& path, Mesh& mesh, const ObjOptions& opt = {}) {
        std::ifstream f(path); if (!f) return false; return load_obj(f, mesh, opt);
    }

    // ============ STL (binary) loader ============
    struct StlOptions { bool compute_normals = true; };

    inline bool load_stl_binary(std::istream& in, Mesh& mesh, const StlOptions& opt = {}) {
        mesh.clear();
        // binary STL: 80‑byte header, 4‑byte uint32 triCount, then triCount * (12 floats + 2 bytes)
        char header[80]; if (!in.read(header, 80)) return false;
        uint32_t triCount = 0; if (!in.read(reinterpret_cast<char*>(&triCount), 4)) return false;
        if (triCount == 0 || triCount > (1u << 28)) return false; // sanity
        mesh.positions.reserve(triCount * 9);
        mesh.indices.reserve(triCount * 3);
        if (opt.compute_normals) mesh.normals.reserve(triCount * 9);

        auto read_f = [&](float& x) { return (bool)in.read(reinterpret_cast<char*>(&x), 4); };

        for (uint32_t t = 0; t < triCount; ++t) {
            float n[3]; if (!(read_f(n[0]) && read_f(n[1]) && read_f(n[2]))) return false;
            float v[9]; for (int i = 0;i < 9;++i) if (!read_f(v[i])) return false;
            uint16_t attr; if (!in.read(reinterpret_cast<char*>(&attr), 2)) return false; (void)attr;
            // append vertices (no dedup — STL has duplicates by design)
            uint32_t base = (uint32_t)(mesh.positions.size() / 3);
            for (int i = 0;i < 9;++i) mesh.positions.push_back(v[i]);
            mesh.indices.push_back(base + 0); mesh.indices.push_back(base + 1); mesh.indices.push_back(base + 2);
            if (opt.compute_normals) {
                auto nn = detail::tri_normal(&v[0], &v[3], &v[6]);
                mesh.normals.insert(mesh.normals.end(), { nn[0],nn[1],nn[2], nn[0],nn[1],nn[2], nn[0],nn[1],nn[2] });
            }
        }
        return true;
    }

    inline bool load_stl_file(const std::string& path, Mesh& mesh, const StlOptions& opt = {}) {
        std::ifstream f(path, std::ios::binary); if (!f) return false; return load_stl_binary(f, mesh, opt);
    }



    // ============ Tiny writer helpers (OBJ) ============
    inline bool write_obj_file(const std::string& path, const Mesh& mesh) {
        std::ofstream out(path); if (!out) return false;
        const size_t V = mesh.positions.size() / 3;
        for (size_t i = 0;i < V;++i) { out << "v " << mesh.positions[i * 3 + 0] << ' ' << mesh.positions[i * 3 + 1] << ' ' << mesh.positions[i * 3 + 2] << '\n'; }
        if (!mesh.normals.empty()) {
            for (size_t i = 0;i < V;++i) { out << "vn " << mesh.normals[i * 3 + 0] << ' ' << mesh.normals[i * 3 + 1] << ' ' << mesh.normals[i * 3 + 2] << '\n'; }
        }
        for (size_t t = 0;t < mesh.indices.size(); t += 3) {
            out << "f ";
            // OBJ is 1‑based
            out << (mesh.indices[t + 0] + 1) << "//" << (mesh.normals.empty() ? 0 : (mesh.indices[t + 0] + 1)) << ' ';
            out << (mesh.indices[t + 1] + 1) << "//" << (mesh.normals.empty() ? 0 : (mesh.indices[t + 1] + 1)) << ' ';
            out << (mesh.indices[t + 2] + 1) << "//" << (mesh.normals.empty() ? 0 : (mesh.indices[t + 2] + 1)) << '\n';
        }
        return true;
    }

} // namespace simple3d
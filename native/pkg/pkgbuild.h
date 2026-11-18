#pragma once
// Builds a fake PS4 app package (like OpenOrbis' PkgTool / LibOrbisPkg PkgBuilder) on the PS4 itself or
// on a PC. A C++ port of the parts of LibOrbisPkg that an app package (pkg_ps4_app, CATEGORY gd)
// needs: the inner PFS (plain), the outer PFS (signed and XTS-encrypted, holding the inner one
// PFSC-wrapped), the PlayGo entries, the license entries and the package header with its digests and
// signature. With the same input, date and time it writes the same bytes as PkgTool.
// Port of LibOrbisPkg (github.com/maxton/LibOrbisPkg), GNU LGPL v3: see COPYING.LESSER and README.md here.

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "crypto.h"
#include "keys.h"

namespace pkgbuild {

using Bytes = std::vector<uint8_t>;
using pkgcrypto::Hash;

// ---- little helpers --------------------------------------------------------------------------
static void put_le(Bytes &b, size_t at, uint64_t v, int n)
{
    for (int i = 0; i < n; ++i) b[at + i] = uint8_t(v >> (8 * i));
}
static void put_be(Bytes &b, size_t at, uint64_t v, int n)
{
    for (int i = 0; i < n; ++i) b[at + i] = uint8_t(v >> (8 * (n - 1 - i)));
}
static void put_bytes(Bytes &b, size_t at, const void *p, size_t n) { memcpy(b.data() + at, p, n); }
static int64_t ceil_div(int64_t a, int64_t b) { return a / b + (a % b == 0 ? 0 : 1); }
static uint64_t align_up(uint64_t v, uint64_t a) { return v % a ? v + (a - v % a) : v; }

// ---- param.sfo ---------------------------------------------------------------------------------
struct SfoValue {
    std::string name;
    uint16_t type;  // 0x204 utf8, 0x404 integer
    std::string text;
    int32_t number = 0;
    int max_length = 4;
    int length() const { return type == 0x404 ? 4 : int(text.size()) + 1; }
    std::string str() const
    {
        if (type != 0x404)
            return text;
        char b[16];
        snprintf(b, sizeof(b), "0x%08x", uint32_t(number));
        return b;
    }
};

struct Sfo {
    std::vector<SfoValue> values;
    void sort() { std::sort(values.begin(), values.end(), [](const SfoValue &a, const SfoValue &b) { return a.name < b.name; }); }
    SfoValue *get(const std::string &name)
    {
        for (auto &v : values)
            if (v.name == name)
                return &v;
        return nullptr;
    }
    void set_text(const std::string &name, const std::string &text, int max_length)
    {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const SfoValue &v) { return v.name == name; }), values.end());
        SfoValue v;
        v.name = name; v.type = 0x204; v.text = text; v.max_length = max_length;
        values.push_back(v);
        sort();
    }
    void set_int(const std::string &name, int32_t number)
    {
        values.erase(std::remove_if(values.begin(), values.end(), [&](const SfoValue &v) { return v.name == name; }), values.end());
        SfoValue v;
        v.name = name; v.type = 0x404; v.number = number; v.max_length = 4;
        values.push_back(v);
        sort();
    }
    // Reads a param.sfo (utf8 and integer values)
    static bool parse(const Bytes &s, Sfo &out)
    {
        auto le = [&](size_t at, int n) { uint32_t v = 0; for (int i = 0; i < n; ++i) v |= uint32_t(s[at + i]) << (8 * i); return v; };
        if (s.size() < 0x14 || s[0] != 0 || s[1] != 'P' || s[2] != 'S' || s[3] != 'F') return false;
        const uint32_t key_table = le(8, 4), data_table = le(12, 4), count = le(16, 4);
        out.values.clear();
        for (uint32_t i = 0; i < count; ++i) {
            const size_t e = 0x14 + 0x10 * size_t(i);
            if (e + 16 > s.size()) return false;
            SfoValue v;
            const uint32_t key_off = le(e, 2), len = le(e + 4, 4), data_off = le(e + 12, 4);
            v.type = uint16_t(le(e + 2, 2));
            v.max_length = int(le(e + 8, 4));
            for (size_t k = key_table + key_off; k < s.size() && s[k]; ++k) v.name += char(s[k]);
            const size_t d = data_table + data_off;
            if (d + (v.type == 0x404 ? 4 : len) > s.size()) return false;
            if (v.type == 0x404)
                v.number = int32_t(le(d, 4));
            else if (v.type == 0x204)
                v.text.assign(reinterpret_cast<const char *>(s.data() + d), len > 0 ? len - 1 : 0);
            else
                v.text.assign(reinterpret_cast<const char *>(s.data() + d), len);
            out.values.push_back(v);
        }
        return true;
    }
    Bytes serialize()
    {
        sort();
        const int key_table = 0x14 + int(values.size()) * 0x10;
        int key_size = 0, data_size = 0;
        for (auto &v : values) { key_size += int(v.name.size()) + 1; data_size += v.max_length; }
        int data_table = key_table + key_size;
        if (data_table % 4) data_table += 4 - data_table % 4;
        Bytes s(size_t(data_table + data_size));
        put_be(s, 0, 0x00505346, 4);
        put_le(s, 4, 0x101, 4);
        put_le(s, 8, key_table, 4);
        put_le(s, 12, data_table, 4);
        put_le(s, 16, values.size(), 4);
        int key_off = 0, data_off = 0, i = 0;
        for (auto &v : values) {
            const size_t e = 0x14 + 0x10 * size_t(i++);
            put_le(s, e, key_off, 2);
            put_le(s, e + 2, v.type, 2);
            put_le(s, e + 4, v.length(), 4);
            put_le(s, e + 8, v.max_length, 4);
            put_le(s, e + 12, data_off, 4);
            put_bytes(s, key_table + key_off, v.name.data(), v.name.size());
            if (v.type == 0x404)
                put_le(s, data_table + data_off, uint32_t(v.number), 4);
            else
                put_bytes(s, data_table + data_off, v.text.data(), v.text.size());
            key_off += int(v.name.size()) + 1;
            data_off += v.max_length;
        }
        return s;
    }
};

// ---- input ---------------------------------------------------------------------------------
struct Project {
    std::string content_id;  // 36 characters
    std::string passcode = "00000000000000000000000000000000";
    int64_t timestamp = 0;   // file times in the PFS (seconds since 1970, UTC)
    std::string c_date;      // "YYYYMMDD" for PUBTOOLINFO
    Sfo sfo;                 // without PUBTOOLINFO / PUBTOOLVER (added here)
    // Files at their path in the package ("eboot.bin", "sce_sys/icon0.png", ...); sce_sys/param.sfo is
    // made from sfo. Paths are sorted like Python's sorted(Path.rglob()) does for create-gp4 projects.
    std::vector<std::pair<std::string, Bytes>> files;
};

// Entry ids of sce_sys files that become package entries instead of PFS files
static const std::map<std::string, uint32_t> &entry_names()
{
    static const std::map<std::string, uint32_t> m = {
        {"param.sfo", 0x1000}, {"icon0.png", 0x1200}, {"pic0.png", 0x1220}, {"pic1.png", 0x1006},
        {"snd0.at9", 0x1240}, {"license.dat", 0x400}, {"license.info", 0x401}, {"nptitle.dat", 0x402},
        {"npbind.dat", 0x403}, {"shareparam.json", 0x1004}, {"save_data.png", 0x1260}};
    return m;
}

// ---- PFS -------------------------------------------------------------------------------------
enum : uint32_t { F_COMPRESSED = 0x1, F_UNK2 = 0x4, F_UNK3 = 0x8, F_READONLY = 0x10, F_INTERNAL = 0x20000 };
enum : uint16_t { M_DIR = 16384, M_FILE = 32768, M_RX = 0x16D };

struct Inode {
    uint32_t number = 0;
    uint16_t mode = 0x16D;
    uint16_t nlink = 0;
    uint32_t flags = 0;
    int64_t size = 0, size_compressed = 0, time = 0;
    uint32_t blocks = 0;
    int32_t db[12] = {0};
    int32_t ib[5] = {0};
    int start_block() const { return db[0]; }
};

struct Dirent {
    uint32_t inode;
    int type;  // 2 file, 3 dir, 4 dot, 5 dotdot
    std::string name;
    int ent_size() const { int e = int(name.size()) + 17; if (e % 8) e += 8 - e % 8; return e; }
    void write(Bytes &b, size_t at) const
    {
        put_le(b, at, inode, 4);
        put_le(b, at + 4, uint32_t(type), 4);
        put_le(b, at + 8, name.size(), 4);
        put_le(b, at + 12, ent_size(), 4);
        put_bytes(b, at + 16, name.data(), name.size());
    }
};

struct Node {
    bool dir = false;
    std::string name;
    Node *parent = nullptr;
    std::vector<Node *> dirs, files;
    std::vector<Dirent> dirents;
    Inode *ino = nullptr;
    // files
    int64_t file_size = 0, compressed_size = 0;
    bool compress = false;
    std::function<void(Bytes &, size_t)> write;  // writes the file's data at an offset
    int64_t size() const
    {
        if (!dir) return file_size;
        int64_t s = 0;
        for (auto &d : dirents) s += d.ent_size();
        return s;
    }
    std::string full_path(const std::string &suffix = "") const
    {
        if (!parent) return suffix;
        return parent->full_path("/" + name + suffix);
    }
    std::vector<Node *> all_dirs()
    {
        std::vector<Node *> r(dirs);
        for (Node *d : dirs)
            for (Node *c : d->all_dirs()) r.push_back(c);
        return r;
    }
    std::vector<Node *> all_files()
    {
        std::vector<Node *> r(files);
        for (Node *d : all_dirs())
            for (Node *f : d->files) r.push_back(f);
        return r;
    }
};

struct PfsProps {
    Node *root = nullptr;
    int64_t file_time = 0;
    uint32_t block_size = 0x10000;
    uint32_t min_blocks = 0;
    bool encrypt = false, sign = false;
    Hash ekpfs;
    Bytes seed;  // 16 bytes when signed or encrypted
};

static uint32_t fpt_hash(const std::string &name)
{
    uint32_t h = 0;
    for (char c : name) h = uint32_t(toupper(static_cast<unsigned char>(c))) + 31 * h;
    return h;
}

class PfsBuilder {
public:
    explicit PfsBuilder(const PfsProps &p) : props(p) { setup(); }
    int64_t size() const { return ndblock * int64_t(props.block_size); }

    Bytes image()
    {
        Bytes img(size_t(size()), 0);
        write_data(img);
        if (props.sign) {
            const Hash key = gen_key(2);
            auto sign = [&](const Sig &s) {
                const Hash h = pkgcrypto::hmac_sha256(key, img.data() + s.block * int64_t(props.block_size), size_t(s.size));
                put_bytes(img, size_t(s.offset), h.data(), 32);
                put_le(img, size_t(s.offset) + 32, uint32_t(int32_t(s.block)), 4);
            };
            for (auto it = data_sigs.rbegin(); it != data_sigs.rend(); ++it) sign(*it);   // a Stack enumerates LIFO
            for (auto it = final_sigs.rbegin(); it != final_sigs.rend(); ++it) sign(*it);
        }
        if (props.encrypt) {
            const Hash k = gen_key(1);  // tweak key, then data key
            pkgcrypto::Xts xts(k.data() + 16, k.data());
            const int64_t total = (size() + 0xFFF) / 0x1000;
            for (int64_t sector = 16; sector < total;) {
                if (sector / 0x10 == empty_block) sector += 16;
                if (sector >= total) break;  // (C#'s generator yields it anyway; out of range would throw there)
                xts.encrypt_sector(img.data() + sector * 0x1000, 0x1000, uint64_t(sector));
                sector += 1;
            }
        }
        return img;
    }

private:
    struct Sig { int64_t block, offset, size; };
    PfsProps props;
    std::vector<std::unique_ptr<Inode>> owned;
    std::vector<Inode *> inodes;
    std::vector<Dirent> super_root_dirents;
    Inode *super_root = nullptr, *fpt_ino = nullptr;
    std::vector<Node *> all_dirs, all_files, all_nodes;
    std::map<uint32_t, uint32_t> fpt;
    std::vector<Sig> final_sigs, data_sigs;
    int64_t ndblock = 0, dinode_count = 0, dinode_blocks = 0;
    Inode header_sig;  // InodeBlockSig (DinodeS64)
    int64_t empty_block = 4;
    Node fpt_node;

    Hash gen_key(uint32_t index) const
    {
        Bytes d(4 + props.seed.size());
        put_le(d, 0, index, 4);
        memcpy(d.data() + 4, props.seed.data(), props.seed.size());
        return pkgcrypto::hmac_sha256(props.ekpfs, d.data(), d.size());
    }
    Inode *make_inode(uint16_t mode, uint32_t blocks, int64_t size = 0, int64_t size_compressed = 0, uint16_t nlink = 1,
                      uint32_t number = 0, uint32_t flags = 0)
    {
        owned.emplace_back(new Inode);
        Inode *i = owned.back().get();
        i->mode = mode; i->blocks = blocks; i->size = size; i->size_compressed = size_compressed; i->nlink = nlink;
        i->number = number; i->flags = props.sign ? flags | F_UNK2 | F_UNK3 : flags; i->time = props.file_time;
        return i;
    }
    int64_t ino_offset(uint32_t number, int db = 0) const { return props.block_size + 0x2C8 * int64_t(number) + 0x64 + 36 * db; }

    void setup()
    {
        final_sigs.push_back({0, 0x380, 0x5A0});
        all_dirs = props.root->all_dirs();
        for (Node *f : props.root->all_files())
            if (!(f->parent && f->parent->name == "sce_sys" && entry_names().count(f->name)))
                all_files.push_back(f);
        std::vector<Node *> dirs_sorted = all_dirs;
        std::stable_sort(dirs_sorted.begin(), dirs_sorted.end(), [](Node *a, Node *b) { return a->full_path() < b->full_path(); });
        all_nodes = dirs_sorted;
        all_nodes.insert(all_nodes.end(), all_files.begin(), all_files.end());
        std::set<uint32_t> seen;
        for (Node *n : all_nodes)
            if (!seen.insert(fpt_hash(n->full_path())).second) {
                fprintf(stderr, "pkgbuild: flat path table collision is not supported\n");
                abort();
            }
        setup_root();
        add_dir_inodes(dirs_sorted);
        std::vector<Node *> files_sorted = all_files;
        std::stable_sort(files_sorted.begin(), files_sorted.end(), [](Node *a, Node *b) { return a->full_path() < b->full_path(); });
        add_file_inodes(files_sorted);
        for (Node *n : all_nodes) fpt[fpt_hash(n->full_path())] = n->ino->number | (n->dir ? 0x20000000u : 0u);
        all_nodes.insert(all_nodes.begin(), props.root);
        layout();
    }
    void setup_root()
    {
        uint32_t number = 0;
        inodes.push_back(super_root = make_inode(M_DIR | M_RX, 1, 65536, 65536, 1, number++, F_INTERNAL | F_READONLY));
        inodes.push_back(fpt_ino = make_inode(M_FILE | M_RX, 1, 0, 0, 1, number++, F_INTERNAL | F_READONLY));
        Inode *uroot = make_inode(M_DIR | M_RX, 1, 65536, 65536, 3, number++, F_READONLY);
        super_root_dirents = {{fpt_ino->number, 2, "flat_path_table"}, {uroot->number, 3, "uroot"}};
        props.root->name = "uroot";
        props.root->ino = uroot;
        props.root->dirents = {{uroot->number, 4, "."}, {uroot->number, 5, ".."}};
        if (props.sign) {
            super_root->flags &= ~F_READONLY;
            fpt_ino->flags &= ~F_READONLY;
            uroot->flags &= ~F_READONLY;
        }
    }
    void add_dir_inodes(const std::vector<Node *> &dirs)
    {
        inodes.push_back(props.root->ino);
        for (Node *d : dirs) {
            Inode *i = make_inode(M_DIR | M_RX, 1, 65536, 0, 2, uint32_t(inodes.size()), F_READONLY);
            d->ino = i;
            d->dirents.push_back({i->number, 4, "."});
            d->dirents.push_back({d->parent->ino->number, 5, ".."});
            d->parent->dirents.push_back({uint32_t(inodes.size()), 3, d->name});
            d->parent->ino->nlink++;
            inodes.push_back(i);
        }
    }
    void add_file_inodes(const std::vector<Node *> &files)
    {
        for (Node *f : files) {
            Inode *i = make_inode(M_FILE | M_RX, uint32_t(ceil_div(f->file_size, props.block_size)), f->file_size, f->compressed_size,
                                  1, uint32_t(inodes.size()), F_READONLY | (f->compress ? F_COMPRESSED : 0));
            if (props.sign) i->flags &= ~F_READONLY;
            f->ino = i;
            f->parent->dirents.push_back({uint32_t(inodes.size()), 2, f->name});
            inodes.push_back(i);
        }
    }
    int64_t indirect_blocks(int64_t size) const
    {
        const int64_t per = props.block_size / 36;
        int64_t blocks = ceil_div(size, props.block_size), ib = 0;
        if (blocks > 12) { blocks -= 12; ib++; }
        if (blocks > per) { blocks -= per; ib += 1 + ceil_div(blocks, per); }
        return ib;
    }
    int64_t round_block(int64_t s) const { return ceil_div(s, props.block_size) * props.block_size; }

    void layout()
    {
        const int64_t bs = props.block_size;
        header_sig.mode = 0; header_sig.nlink = 1; header_sig.flags = F_READONLY;
        if (props.sign) {
            ndblock = 1;
            const int64_t per_block = bs / 0x2C8;
            dinode_count = int64_t(inodes.size());
            dinode_blocks = ceil_div(dinode_count, per_block);
            header_sig.blocks = uint32_t(dinode_blocks);
            header_sig.size = header_sig.size_compressed = dinode_blocks * bs;
            header_sig.time = props.file_time;
            header_sig.flags = 0;
            for (int i = 0; i < dinode_blocks; ++i) {
                header_sig.db[i] = 1 + i;
                final_sigs.push_back({1 + i, 0xB8 + 36 * i, 0x10000});
            }
            ndblock += dinode_blocks;
            super_root->db[0] = int32_t(dinode_blocks + 1);
            final_sigs.push_back({super_root->start_block(), ino_offset(super_root->number), 0x10000});
            ndblock += super_root->blocks;
            fpt_ino->db[0] = super_root->start_block() + 1;
            fpt_ino->size = fpt_ino->size_compressed = int64_t(fpt.size()) * 8;
            fpt_ino->blocks = uint32_t(ceil_div(fpt_ino->size, bs));
            final_sigs.push_back({fpt_ino->start_block(), ino_offset(fpt_ino->number), 0x10000});
            for (int i = 1; i < int(fpt_ino->blocks) && i < 12; i++) {
                fpt_ino->db[i] = int32_t(ndblock++);
                final_sigs.push_back({fpt_ino->start_block(), ino_offset(fpt_ino->number, i), 0x10000});
            }
            ndblock++;
            empty_block = ndblock;
            ndblock++;
            int64_t ib_start = ndblock;
            for (Node *n : all_nodes) ndblock += indirect_blocks(n->size());
            const int64_t per = bs / 36;
            for (Node *n : all_nodes) {
                const int64_t blocks = ceil_div(n->size(), bs);
                n->ino->db[0] = int32_t(ndblock);
                n->ino->blocks = uint32_t(blocks);
                n->ino->size = n->dir ? round_block(n->size()) : n->size();
                if (n->ino->size_compressed == 0) n->ino->size_compressed = n->ino->size;
                for (int i = 0; (blocks - i) > 0 && i < 12; i++)
                    data_sigs.push_back({ndblock++, ino_offset(n->ino->number, i), 0x10000});
                if (blocks > 12) {
                    final_sigs.push_back({ib_start, ino_offset(n->ino->number, 12), 0x10000});
                    for (int64_t i = 12, p = 0; (blocks - i) > 0 && i < 12 + per; i++, p += 36)
                        data_sigs.push_back({ndblock++, ib_start * bs + p, 0x10000});
                    ib_start++;
                }
                if (blocks > 12 + per) {
                    fprintf(stderr, "pkgbuild: doubly indirect blocks are not supported\n");
                    abort();
                }
            }
        } else {
            ndblock = 1;
            const int64_t per_block = bs / 0xA8;
            dinode_count = int64_t(inodes.size());
            dinode_blocks = ceil_div(dinode_count, per_block);
            header_sig.blocks = uint32_t(dinode_blocks);
            header_sig.size = header_sig.size_compressed = dinode_blocks * bs;
            header_sig.db[0] = int32_t(ndblock++);
            header_sig.time = props.file_time;
            for (int i = 1; i < dinode_blocks; i++) {
                if (i < 12) header_sig.db[i] = -1;
                ndblock++;
            }
            super_root->db[0] = int32_t(ndblock);
            ndblock += super_root->blocks;
            fpt_ino->db[0] = int32_t(ndblock++);
            fpt_ino->size = fpt_ino->size_compressed = int64_t(fpt.size()) * 8;
            fpt_ino->blocks = uint32_t(ceil_div(fpt_ino->size, bs));
            for (int i = 1; i < int(fpt_ino->blocks) && i < 12; i++) fpt_ino->db[i] = int32_t(ndblock++);
            ndblock++;  // no collision resolver: an empty block
            for (Node *n : all_nodes) {
                const int64_t blocks = ceil_div(n->size(), bs);
                n->ino->db[0] = int32_t(ndblock);
                n->ino->blocks = uint32_t(blocks);
                n->ino->size = n->dir ? round_block(n->size()) : n->size();
                if (n->ino->size_compressed == 0) n->ino->size_compressed = n->ino->size;
                for (int i = 1; i < blocks && i < 12; i++) n->ino->db[i] = -1;
                ndblock += blocks;
            }
        }
        ndblock = std::max<int64_t>(ndblock, props.min_blocks);
    }

    void write_inode_common(Bytes &b, size_t at, const Inode &i, bool pad64)
    {
        put_le(b, at, i.mode, 2);
        put_le(b, at + 2, i.nlink, 2);
        put_le(b, at + 4, i.flags, 4);
        put_le(b, at + 8, uint64_t(i.size), 8);
        put_le(b, at + 16, uint64_t(i.size_compressed), 8);
        for (int t = 0; t < 4; ++t) put_le(b, at + 24 + 8 * t, uint64_t(i.time), 8);
        // nsec, uid, gid, unk1, unk2: zero
        put_le(b, at + 96, i.blocks, 4);
        (void)pad64;
    }
    // returns the size written
    size_t write_inode(Bytes &b, size_t at, const Inode &i)
    {
        write_inode_common(b, at, i, false);
        if (props.sign) {  // DinodeS32: 12 + 5 x (32-byte signature, int32 block)
            for (int k = 0; k < 12; ++k) put_le(b, at + 100 + 36 * k + 32, uint32_t(i.db[k]), 4);
            for (int k = 0; k < 5; ++k) put_le(b, at + 100 + 36 * (12 + k) + 32, uint32_t(i.ib[k]), 4);
            return 0x2C8;
        }
        for (int k = 0; k < 12; ++k) put_le(b, at + 100 + 4 * k, uint32_t(i.db[k]), 4);
        for (int k = 0; k < 5; ++k) put_le(b, at + 148 + 4 * k, uint32_t(i.ib[k]), 4);
        return 0xA8;
    }
    void write_header(Bytes &b)
    {
        put_le(b, 0, 1, 8);                    // version
        put_le(b, 8, 20130315, 8);             // magic
        put_le(b, 16, 0, 8);                   // id
        b[24] = 0; b[25] = 0; b[26] = 1; b[27] = 0;  // fmode, clean, readonly, rsv
        put_le(b, 28, (props.sign ? 1 : 0) | (props.encrypt ? 4 : 0) | 8, 2);
        put_le(b, 30, 0, 2);
        put_le(b, 32, props.block_size, 4);
        put_le(b, 36, 0, 4);                   // nbackup
        put_le(b, 40, 1, 8);                   // nblock
        put_le(b, 48, uint64_t(dinode_count), 8);
        put_le(b, 56, uint64_t(ndblock), 8);
        put_le(b, 64, uint64_t(dinode_blocks), 8);
        put_le(b, 72, 0, 8);
        // InodeBlockSig, a DinodeS64 at 0x50
        write_inode_common(b, 0x50, header_sig, true);
        put_le(b, 0x50 + 100, 0, 4);  // padding
        for (int k = 0; k < 12; ++k) put_le(b, 0x50 + 104 + 40 * k + 32, uint64_t(int64_t(header_sig.db[k])), 8);
        for (int k = 0; k < 5; ++k) put_le(b, 0x50 + 104 + 40 * (12 + k) + 32, uint64_t(int64_t(header_sig.ib[k])), 8);
        if (props.sign || props.encrypt) {
            put_le(b, 0x36C, 1, 4);
            put_bytes(b, 0x370, props.seed.data(), props.seed.size());
        } else {
            put_le(b, 0x368, 1, 4);
        }
    }
    void write_data(Bytes &b)
    {
        const int64_t bs = props.block_size;
        write_header(b);
        size_t pos = size_t(bs);
        const size_t isz = props.sign ? 0x2C8 : 0xA8;
        for (Inode *i : inodes) {
            pos += write_inode(b, pos, *i);
            if (pos % bs > bs - isz) pos += bs - pos % bs;
        }
        pos = size_t(bs * (dinode_blocks + 1));
        for (auto &d : super_root_dirents) { d.write(b, pos); pos += size_t(d.ent_size()); }
        // flat path table
        size_t at = size_t(fpt_ino->start_block() * bs);
        for (auto &kv : fpt) { put_le(b, at, kv.first, 4); put_le(b, at + 4, kv.second, 4); at += 8; }
        for (Node *n : all_nodes) {
            size_t p = size_t(n->ino->start_block() * bs);
            if (n->dir) {
                int64_t block = n->ino->start_block();
                for (auto &d : n->dirents) {
                    d.write(b, p);
                    p += size_t(d.ent_size());
                    if (int64_t(p % bs) > bs - 280) p = size_t(++block * bs);
                }
            } else {
                n->write(b, p);
            }
        }
    }
};

// ---- the package --------------------------------------------------------------------------
static Hash compute_keys(const std::string &content_id, const std::string &passcode, uint32_t index)
{
    uint8_t data[96] = {0};
    uint8_t idx[4] = {uint8_t(index >> 24), uint8_t(index >> 16), uint8_t(index >> 8), uint8_t(index)};
    const Hash a = pkgcrypto::sha256(idx, 4);
    uint8_t cid[48] = {0};
    memcpy(cid, content_id.data(), std::min<size_t>(48, content_id.size()));
    const Hash b = pkgcrypto::sha256(cid, 48);
    memcpy(data, a.data(), 32);
    memcpy(data + 32, b.data(), 32);
    memcpy(data + 64, passcode.data(), 32);
    return pkgcrypto::sha256(data, 96);
}

// The Mersenne Twister variant of LibOrbisPkg (seeded by an array)
struct MT {
    uint32_t mt[624];
    uint32_t mti = 624;
    explicit MT(const uint32_t *seed, size_t n)
    {
        mt[0] = 0x12BD6AA;
        for (mti = 1; mti < 624; mti++) mt[mti] = mti + 0x6C078965u * (mt[mti - 1] ^ (mt[mti - 1] >> 30));
        uint32_t s = 1, k = 0;
        for (size_t len = std::max<size_t>(624, n); len > 0; len--) {
            mt[s] = (mt[s] ^ ((mt[s - 1] ^ (mt[s - 1] >> 30)) * 0x19660Du)) + seed[k] + k;
            s++; k++;
            if (s >= 624) { mt[0] = mt[623]; s = 1; }
            if (k >= n) k = 0;
        }
        for (int len = 0; len < 623; len++) {
            mt[s] = (mt[s] ^ ((mt[s - 1] ^ (mt[s - 1] >> 30)) * 0x5D588B65u)) - s;
            s++;
            if (s >= 624) { mt[0] = mt[623]; s = 1; }
        }
        mt[0] = 1u << 31;
    }
    uint32_t next()
    {
        static const uint32_t mag[2] = {0, 0x9908b0df};
        uint32_t y;
        if (mti >= 624) {
            int kk;
            for (kk = 0; kk < 624 - 397; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag[y & 1];
            }
            for (; kk < 623; kk++) {
                y = (mt[kk] & 0x80000000u) | (mt[kk + 1] & 0x7fffffffu);
                mt[kk] = mt[kk + 397 - 624] ^ (y >> 1) ^ mag[y & 1];
            }
            y = (mt[623] & 0x80000000u) | (mt[0] & 0x7fffffffu);
            mt[623] = mt[396] ^ (y >> 1) ^ mag[y & 1];
            mti = 0;
        }
        y = mt[mti++];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9d2c5680u;
        y ^= (y << 15) & 0xefc60000u;
        y ^= (y >> 18);
        return y;
    }
};

// RSA2048EncryptKey: deterministic PKCS#1-type-2 padding from a Mersenne Twister, then RSA
static Bytes rsa_encrypt_key(const uint8_t modulus[256], const uint8_t *value, size_t n)
{
    Bytes buf(256 + 32, 0);
    memcpy(buf.data(), modulus, 256);
    memcpy(buf.data() + 256, value, std::min<size_t>(n, 32));
    const Hash h = pkgcrypto::sha256(pkgcrypto::sha256(buf));
    uint32_t seed[8];
    for (int i = 0; i < 8; ++i) seed[i] = uint32_t(h[4 * i]) << 24 | uint32_t(h[4 * i + 1]) << 16 | uint32_t(h[4 * i + 2]) << 8 | h[4 * i + 3];
    MT mt(seed, 8);
    uint8_t padded[256] = {0};
    padded[1] = 2;
    memcpy(padded + 224, value, std::min<size_t>(n, 32));
    for (int k = 2; k < 223;) {
        uint8_t src[48];
        for (int i = 0; i < 12; ++i) {
            const uint32_t v = mt.next();
            src[4 * i] = uint8_t(v >> 24); src[4 * i + 1] = uint8_t(v >> 16); src[4 * i + 2] = uint8_t(v >> 8); src[4 * i + 3] = uint8_t(v);
        }
        const Hash r = pkgcrypto::sha256(src, 48);
        for (uint8_t x : r) {
            if (k >= 223) break;
            if (x != 0) padded[k++] = x;
        }
    }
    return pkgcrypto::rsa_public(modulus, padded);
}

struct Entry {
    uint32_t id;
    std::string name;  // "" when it has no name
    Bytes data;
    uint32_t data_offset = 0, data_size = 0, name_offset = 0, flags1 = 0, flags2 = 0;
    uint32_t key_index() const { return (flags2 & 0xF000) >> 12; }
    bool encrypted() const { return (flags1 & 0x80000000u) != 0; }
    Bytes meta() const
    {
        Bytes m(32, 0);
        put_be(m, 0, id, 4); put_be(m, 4, name_offset, 4); put_be(m, 8, flags1, 4);
        put_be(m, 12, flags2, 4); put_be(m, 16, data_offset, 4); put_be(m, 20, data_size, 4);
        return m;
    }
};

// Builds the package. log() gets progress lines (may be empty).
static Bytes build(Project p, const std::function<void(const char *)> &log = nullptr)
{
    auto say = [&](const char *s) { if (log) log(s); };
    const Hash ekpfs = compute_keys(p.content_id, p.passcode, 1);

    // ---- file tree (sce_sys/param.sfo from the sfo; keystone added) ----
    std::vector<std::unique_ptr<Node>> nodes;
    Node root;
    root.dir = true;
    std::map<std::string, Node *> dirs;
    dirs[""] = &root;
    std::function<Node *(const std::string &)> dir_of = [&](const std::string &path) -> Node * {
        auto it = dirs.find(path);
        if (it != dirs.end()) return it->second;
        const size_t slash = path.rfind('/');
        Node *parent = dir_of(slash == std::string::npos ? "" : path.substr(0, slash));
        nodes.emplace_back(new Node);
        Node *d = nodes.back().get();
        d->dir = true;
        d->name = slash == std::string::npos ? path : path.substr(slash + 1);
        d->parent = parent;
        parent->dirs.push_back(d);
        return dirs[path] = d;
    };
    // Sorted like Python's sorted(Path("."). rglob("*")): by path parts. Directories are created in that
    // order (so before their files), then files are added in the same order.
    auto parts_less = [](const std::string &a, const std::string &b) {
        size_t i = 0, j = 0;
        for (;;) {
            const size_t ea = a.find('/', i), eb = b.find('/', j);
            const std::string pa = a.substr(i, ea == std::string::npos ? std::string::npos : ea - i);
            const std::string pb = b.substr(j, eb == std::string::npos ? std::string::npos : eb - j);
            if (pa != pb) return pa < pb;
            if (ea == std::string::npos || eb == std::string::npos) return ea == std::string::npos && eb != std::string::npos;
            i = ea + 1; j = eb + 1;
        }
    };
    std::vector<std::string> entries_order;
    std::map<std::string, Bytes *> file_data;
    for (auto &f : p.files) { entries_order.push_back(f.first); file_data[f.first] = &f.second; }
    const Bytes sfo_placeholder;
    if (!file_data.count("sce_sys/param.sfo")) entries_order.push_back("sce_sys/param.sfo");
    std::vector<std::string> all_paths;  // dirs and files, like rglob
    {
        std::set<std::string> seen;
        for (auto &path : entries_order) {
            size_t at = 0;
            while ((at = path.find('/', at)) != std::string::npos) {
                if (seen.insert(path.substr(0, at)).second) all_paths.push_back(path.substr(0, at));
                ++at;
            }
            all_paths.push_back(path);
        }
        std::sort(all_paths.begin(), all_paths.end(), parts_less);
    }
    std::set<std::string> is_file(entries_order.begin(), entries_order.end());
    for (auto &path : all_paths)
        if (!is_file.count(path)) dir_of(path);
    Bytes sfo_bytes;
    for (auto &path : all_paths) {
        if (!is_file.count(path)) continue;
        const size_t slash = path.rfind('/');
        Node *parent = dir_of(slash == std::string::npos ? "" : path.substr(0, slash));
        nodes.emplace_back(new Node);
        Node *f = nodes.back().get();
        f->name = slash == std::string::npos ? path : path.substr(slash + 1);
        f->parent = parent;
        if (path == "sce_sys/param.sfo") {
            f->file_size = f->compressed_size = 0;  // an entry, not in the PFS
        } else {
            const Bytes *data = file_data[path];
            f->file_size = f->compressed_size = int64_t(data->size());
            f->write = [data](Bytes &b, size_t at) { memcpy(b.data() + at, data->data(), data->size()); };
        }
        parent->files.push_back(f);
    }
    // keystone (inner PFS only)
    Node *sce_sys = dir_of("sce_sys");
    Bytes keystone;
    {
        const char *hdr_hex = "6b657973746f6e65020001000000000000000000000000000000000000000000";
        for (int i = 0; i < 32; ++i) { unsigned v; sscanf(hdr_hex + 2 * i, "%2x", &v); keystone.push_back(uint8_t(v)); }
        const Hash fingerprint = pkgcrypto::hmac_sha256(pkgkeys::keystone_hmac_key, 32, p.passcode.data(), p.passcode.size());
        Bytes hf(keystone);
        hf.insert(hf.end(), fingerprint.begin(), fingerprint.end());
        const Hash final_ = pkgcrypto::hmac_sha256(pkgkeys::keystone_mac_data, 32, hf.data(), hf.size());
        keystone = hf;
        keystone.insert(keystone.end(), final_.begin(), final_.end());
    }
    nodes.emplace_back(new Node);
    Node *ks = nodes.back().get();
    ks->name = "keystone";
    ks->parent = sce_sys;
    ks->file_size = ks->compressed_size = int64_t(keystone.size());
    ks->write = [&keystone](Bytes &b, size_t at) { memcpy(b.data() + at, keystone.data(), keystone.size()); };
    sce_sys->files.push_back(ks);

    // ---- inner PFS ----
    say("PFS opbouwen...");
    PfsProps inner_props;
    inner_props.root = &root;
    inner_props.min_blocks = 0x55;
    inner_props.file_time = p.timestamp;
    PfsBuilder inner(inner_props);
    const int64_t inner_size = inner.size();

    // ---- outer PFS: one file, the inner image PFSC-wrapped ----
    const int64_t pfsc_blocks = (inner_size + 0x10000 - 1) / 0x10000;
    const int64_t ptr_table = 8 + pfsc_blocks * 8;
    const int64_t extra = ((ptr_table - 0xFC00) + 0xFFFF) / 0x10000;
    const int64_t pfsc_header = 0x10000 + (extra > 0 ? 0x10000 * extra : 0);
    Node outer_root;
    outer_root.dir = true;
    Node image_file;
    image_file.name = "pfs_image.dat";
    image_file.parent = &outer_root;
    image_file.file_size = inner_size + pfsc_header;
    image_file.compressed_size = inner_size;
    image_file.compress = true;
    image_file.write = [&](Bytes &b, size_t at) {
        put_be(b, at, 0x50465343, 4);
        put_le(b, at + 4, 0, 4);
        put_le(b, at + 8, 6, 4);
        put_le(b, at + 12, 0x10000, 4);
        put_le(b, at + 16, 0x10000, 8);
        put_le(b, at + 24, 0x400, 8);
        put_le(b, at + 32, uint64_t(pfsc_header), 8);
        put_le(b, at + 40, uint64_t(pfsc_blocks * 0x10000), 8);
        for (int64_t i = 0; i <= pfsc_blocks; i++) put_le(b, at + 0x400 + size_t(8 * i), uint64_t(pfsc_header + i * 0x10000), 8);
        const Bytes img = inner.image();
        memcpy(b.data() + at + pfsc_header, img.data(), img.size());
    };
    outer_root.files.push_back(&image_file);
    PfsProps outer_props;
    outer_props.root = &outer_root;
    outer_props.encrypt = true;
    outer_props.sign = true;
    outer_props.ekpfs = ekpfs;
    outer_props.seed = Bytes(16, 0);
    outer_props.file_time = p.timestamp;
    PfsBuilder outer(outer_props);
    const int64_t pfs_size = outer.size();

    // ---- header and entries ----
    uint64_t package_size = 0x80000 + uint64_t(pfs_size);
    {
        char info[200];
        snprintf(info, sizeof(info), "c_date=%s,img0_l0_size=%llu,img0_l1_size=0,img0_sc_ksize=512,img0_pc_ksize=832", p.c_date.c_str(),
                 (unsigned long long)((package_size + 0xFFFFF) / (1024 * 1024)));
        p.sfo.set_text("PUBTOOLINFO", info, 0x200);
        p.sfo.set_int("PUBTOOLVER", 0x02890000);
    }
    sfo_bytes = p.sfo.serialize();

    std::vector<Entry> entries;
    auto add = [&](uint32_t id, const std::string &name, Bytes data) { Entry e; e.id = id; e.name = name; e.data = std::move(data); entries.push_back(e); };
    // ENTRY_KEYS
    Bytes keys(2048, 0);
    {
        uint8_t cid[48] = {0};
        memcpy(cid, p.content_id.data(), 36);
        const Hash seed = pkgcrypto::sha256(cid, 48);
        memcpy(keys.data(), seed.data(), 32);
        for (uint32_t i = 0; i < 7; ++i) {
            const Hash k = compute_keys(p.content_id, p.passcode, i);
            Hash d = pkgcrypto::sha256(k);
            for (int j = 0; j < 32; ++j) d[j] ^= k[j];
            memcpy(keys.data() + 32 + 32 * i, d.data(), 32);
            const Bytes enc = i == 0 ? rsa_encrypt_key(pkgkeys::pkg_public_keys[0], reinterpret_cast<const uint8_t *>(p.passcode.data()), 32)
                                     : rsa_encrypt_key(pkgkeys::pkg_public_keys[i], k.data(), 32);
            memcpy(keys.data() + 32 + 224 + 256 * i, enc.data(), 256);
        }
    }
    add(0x10, "", keys);
    add(0x20, "", rsa_encrypt_key(pkgkeys::fake_modulus, ekpfs.data(), 32));       // IMAGE_KEY
    add(0x80, "", Bytes(0x180, 0));                                                // GENERAL_DIGESTS (later)
    add(0x100, "", Bytes());                                                       // METAS (later)
    add(0x1, "", Bytes());                                                         // DIGESTS (later)
    add(0x200, "", Bytes());                                                       // ENTRY_NAMES (later)
    // PlayGo
    Bytes chunk_dat(416, 0);
    add(0x1001, "playgo-chunk.dat", Bytes());
    add(0x1002, "playgo-chunk.sha", Bytes());
    {
        const char *xml =
            "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\"?>\r\n"
            "<psproject fmt=\"playgo-manifest\" version=\"0990\">\r\n"
            "  <volume>\r\n"
            "    <chunk_info chunk_count=\"1\" scenario_count=\"1\">\r\n"
            "      <scenarios default_id=\"0\">\r\n"
            "        <scenario id=\"0\" type=\"sp\" initial_chunk_count=\"1\" label=\"Scenario #0\">0</scenario>\r\n"
            "      </scenarios>\r\n"
            "    </chunk_info>\r\n"
            "  </volume>\r\n"
            "</psproject>\r\n";
        // LibOrbisPkg casts each C# char to a byte: its BOM "ï»¿" becomes EF BB BF here too
        add(0x1003, "playgo-manifest.xml", Bytes(xml, xml + strlen(xml)));
    }
    // LICENSE_DAT
    {
        Bytes lic(0x400, 0);
        put_be(lic, 0, 0x52494600, 4);
        put_be(lic, 4, 1, 2);
        put_be(lic, 6, 0xFFFF, 2);
        put_be(lic, 8, 0, 8);
        put_be(lic, 16, 1364222275ULL, 8);
        put_be(lic, 24, 0x7FFFFFFFFFFFFFFFULL, 8);
        put_bytes(lic, 32, p.content_id.data(), 36);
        put_be(lic, 80, 0x200, 2);   // license type: debug
        put_be(lic, 82, 0xF, 2);     // drm: PS4
        put_be(lic, 84, 0x1A, 2);    // content type: GD
        put_be(lic, 86, 3, 2);       // sku flag
        put_be(lic, 88, 0, 4); put_be(lic, 92, 0, 4); put_be(lic, 96, 0, 4); put_be(lic, 100, 1, 4); put_be(lic, 104, 0, 4);
        // 108 + 468 = 576 (0x240): disc key (32), secret iv (16), secret (144), signature (256)
        uint8_t cid[48] = {0};
        memcpy(cid, p.content_id.data(), 36);
        const Hash tmp = pkgcrypto::sha256(cid, 48);
        uint8_t iv[16], secret[144] = {0};
        memcpy(iv, tmp.data(), 16);
        memcpy(secret, tmp.data() + 16, 16);
        pkgcrypto::aes_cbc_encrypt(secret, 144, pkgkeys::rif_debug_key, iv);
        put_bytes(lic, 0x260, iv, 16);
        put_bytes(lic, 0x270, secret, 144);
        const Hash h = pkgcrypto::sha256(lic.data(), 0x300);
        const Bytes sig = pkgcrypto::rsa_sign_sha256(pkgkeys::rif_modulus, pkgkeys::rif_private_exponent, h.data());
        put_bytes(lic, 0x300, sig.data(), 256);
        add(0x400, "", lic);
    }
    // LICENSE_INFO
    {
        Bytes info(0x200, 0);
        put_bytes(info, 0, p.content_id.data(), 36);
        put_be(info, 64, 0, 4);       // unknown_40 (1 for AL)
        put_be(info, 68, 0x1A, 4);    // content type
        put_be(info, 72, 0, 4);
        put_be(info, 76, 1, 4);
        add(0x401, "", info);
    }
    add(0x1000, "param.sfo", sfo_bytes);
    add(0x409, "", Bytes(0x2000, 0));  // PSRESERVED_DAT
    for (Node *f : sce_sys->files) {
        auto it = entry_names().find(f->name);
        if (it == entry_names().end() || f->name == "param.sfo") continue;
        add(it->second, f->name, *file_data["sce_sys/" + f->name]);
    }
    Entry *metas = nullptr, *digests = nullptr, *names = nullptr, *chunk_sha = nullptr, *general = nullptr;
    for (auto &e : entries) {
        if (e.id == 0x100) metas = &e;
        if (e.id == 0x1) digests = &e;
        if (e.id == 0x200) names = &e;
        if (e.id == 0x1002) chunk_sha = &e;
        if (e.id == 0x80) general = &e;
    }
    digests->data.assign(entries.size() * 32, 0);
    metas->data.assign(entries.size() * 32, 0);
    // name table: names in sorted order (entries without a name share offset 0)
    std::vector<std::string> name_list = {""};
    std::map<std::string, uint32_t> name_offsets = {{"", 0}};
    {
        uint32_t len = 1;
        std::vector<std::string> sorted;
        for (auto &e : entries) sorted.push_back(e.name);
        std::stable_sort(sorted.begin(), sorted.end());
        for (auto &n : sorted)
            if (!n.empty() && !name_offsets.count(n)) { name_offsets[n] = len; name_list.push_back(n); len += uint32_t(n.size()) + 1; }
        Bytes table;
        for (auto &n : name_list) { table.insert(table.end(), n.begin(), n.end()); table.push_back(0); }
        names->data = table;
    }
    const std::map<uint32_t, uint32_t> flag_map = {{0x1, 0x40000000}, {0x10, 0x60000000}, {0x20, 0xE0000000}, {0x80, 0x60000000},
                                                   {0x100, 0x60000000}, {0x200, 0x40000000}, {0x400, 0x80000000}, {0x401, 0x80000000}};
    const std::map<uint32_t, uint32_t> key_map = {{0x20, 3u << 12}, {0x400, 3u << 12}, {0x401, 2u << 12}};
    uint64_t data_offset = 0x2000;
    uint64_t entries_length_sum = 0;
    for (auto &e : entries) entries_length_sum += align_up(e.data.size(), 16);
    for (auto &e : entries) {
        e.name_offset = name_offsets[e.name];
        e.data_offset = uint32_t(data_offset);
        e.data_size = uint32_t(e.data.size());
        e.flags1 = flag_map.count(e.id) ? flag_map.at(e.id) : 0;
        e.flags2 = key_map.count(e.id) ? key_map.at(e.id) : 0;
        if (&e == chunk_sha) {
            // estimate of the package size without the chunk hashes (as LibOrbisPkg)
            int64_t pkg_size = int64_t(align_up(0x2000 + entries_length_sum, 0x80000)) + pfs_size;
            pkg_size += ((pkg_size + 16) / 0x10000) * 4;
            e.data_size = uint32_t(pkg_size / 0x10000) * 4;
        }
        if (e.id == 0x1001) e.data_size = 416;
        data_offset = align_up(data_offset + e.data_size, 16);
    }
    const uint64_t body_size_raw = data_offset - 0x2000;
    const uint64_t body_size = align_up(0x2000 + body_size_raw, 0x80000) - 0x2000;
    const uint64_t pfs_offset = 0x2000 + body_size;
    package_size = body_size + 0x2000 + uint64_t(pfs_size);
    chunk_sha->data.assign(4 * (package_size / 0x10000), 0);
    chunk_sha->data_size = uint32_t(chunk_sha->data.size());
    // playgo-chunk.dat
    {
        Bytes &c = chunk_dat;
        put_le(c, 0, 0x6f676c70, 4);
        put_le(c, 8, 1, 2); put_le(c, 10, 1, 2); put_le(c, 12, 1, 2); put_le(c, 14, 1, 2);
        put_le(c, 16, 416, 4);
        put_le(c, 20, 0, 2); put_le(c, 22, 1, 2);
        memset(c.data() + 32, 0xff, 32);
        put_bytes(c, 64, p.content_id.data(), 36);
        const uint32_t table[16] = {256, 32, 288, 2, 304, 9, 320, 16, 352, 32, 384, 2, 400, 12, 336, 16};
        for (int i = 0; i < 16; ++i) put_le(c, 0xC0 + 4 * i, table[i], 4);
        c[0x100] = 0x80; c[0x101] = 0; c[0x102] = 3;
        put_le(c, 0x100 + 3 + 0xB, 1, 2);
        put_le(c, 0x100 + 16, 0xFFFFFFFFFFFFFFFFULL, 8);
        put_le(c, 0x120, 0, 2);
        put_bytes(c, 0x130, "Chunk #0", 8);
        put_le(c, 0x140, 0, 8); put_le(c, 0x148, package_size, 8);
        put_le(c, 0x150, 0, 8); put_le(c, 0x158, uint64_t(inner_size), 8);
        c[0x160] = 1;
        put_le(c, 0x160 + 1 + 0x13, 1, 2);
        put_le(c, 0x160 + 22, 1, 2);
        put_le(c, 0x180, 0, 2);
        put_bytes(c, 0x190, "Scenario #0", 11);
        for (auto &e : entries) if (e.id == 0x1001) e.data = c;
    }

    // ---- write it ----
    Bytes pkg(size_t(0x2000 + body_size + uint64_t(pfs_size)), 0);
    say("Versleutelen en ondertekenen...");
    {
        const Bytes img = outer.image();
        memcpy(pkg.data() + pfs_offset, img.data(), img.size());
    }
    // PlayGo chunk hashes
    for (uint64_t i = pfs_offset / 0x10000; i < pkg.size() / 0x10000; i++) {
        const Hash h = pkgcrypto::sha256(pkg.data() + i * 0x10000, 0x10000);
        memcpy(chunk_sha->data.data() + i * 4, h.data(), 4);
    }
    // header fields
    const uint32_t entry_count = uint32_t(entries.size());
    uint32_t main_ent = 0;
    for (auto &e : entries)
        if (e.id == 0x10 || e.id == 0x20 || e.id == 0x80 || e.id == 0x100 || e.id == 0x1) main_ent += uint32_t(e.data.size());
    const Hash pfs_signed_digest = pkgcrypto::sha256(pkg.data() + pfs_offset, 0x10000);
    const Hash pfs_image_digest = pkgcrypto::sha256(pkg.data() + pfs_offset, size_t(pfs_size));
    auto write_header = [&](Bytes &b) {
        memcpy(b.data(), "\x7f" "CNT", 4);
        put_be(b, 0x04, 1, 4);                 // flags
        put_be(b, 0x08, 0, 4);
        put_be(b, 0x0C, 0xF, 4);
        put_be(b, 0x10, entry_count, 4);
        put_be(b, 0x14, 6, 2);                 // sc entry count
        put_be(b, 0x16, entry_count, 2);
        put_be(b, 0x18, 0x2A80, 4);
        put_be(b, 0x1C, main_ent, 4);
        put_be(b, 0x20, 0x2000, 8);
        put_be(b, 0x28, body_size, 8);
        memcpy(b.data() + 0x40, p.content_id.data(), 36);
        put_be(b, 0x70, 0xF, 4);
        put_be(b, 0x74, 0x1A, 4);
        put_be(b, 0x78, 0x0A000000, 4);
        put_be(b, 0x7C, uint32_t(body_size + 0x2000), 4);  // promote size
        put_be(b, 0x80, 0x20161020, 4);
        put_be(b, 0x84, 0x1738551, 4);
        put_be(b, 0x98, 0, 4);
        put_be(b, 0x9C, 1, 4);
        put_be(b, 0x400, 1, 4);
        put_be(b, 0x404, 1, 4);
        put_be(b, 0x408, 0x80000000000003CCULL, 8);
        put_be(b, 0x410, pfs_offset, 8);
        put_be(b, 0x418, uint64_t(pfs_size), 8);
        put_be(b, 0x420, 0, 8);
        put_be(b, 0x428, package_size, 8);    // mount image size (as LibOrbisPkg)
        put_be(b, 0x430, package_size, 8);
        put_be(b, 0x438, 0x10000, 4);
        put_be(b, 0x43C, 0xD0000, 4);
        put_bytes(b, 0x440, pfs_image_digest.data(), 32);
        put_bytes(b, 0x460, pfs_signed_digest.data(), 32);
    };
    // general digests (from the header as it is now: hashes still zero)
    {
        Bytes h(0x5A0, 0);
        write_header(h);
        pkgcrypto::Sha256 s;
        s.update(h.data(), 64);
        s.update(h.data() + 0x400, 128);
        Hash header_digest(32);
        s.finish(header_digest.data());
        std::string major = "ATTRIBUTE" + (p.sfo.get("ATTRIBUTE") ? p.sfo.get("ATTRIBUTE")->str() : std::string());
        if (p.sfo.get("ATTRIBUTE2")) major += "ATTRIBUTE2" + p.sfo.get("ATTRIBUTE2")->str();
        major += "CATEGORY" + (p.sfo.get("CATEGORY") ? p.sfo.get("CATEGORY")->str() : std::string());
        major += "FORMAT" + (p.sfo.get("FORMAT") ? p.sfo.get("FORMAT")->str() : std::string());
        major += "PUBTOOLVER" + (p.sfo.get("PUBTOOLVER") ? p.sfo.get("PUBTOOLVER")->str() : std::string());
        const Hash major_digest = pkgcrypto::sha256(major.data(), major.size());
        Bytes cd(48 + 8 + 32 + 32, 0);
        memcpy(cd.data(), p.content_id.data(), 36);
        put_be(cd, 48, 0xF, 4);
        put_be(cd, 52, 0x1A, 4);
        put_bytes(cd, 56, pfs_image_digest.data(), 32);
        put_bytes(cd, 88, major_digest.data(), 32);
        const Hash content_digest = pkgcrypto::sha256(cd);
        const Hash param_digest = pkgcrypto::sha256(sfo_bytes);
        Bytes &g = general->data;
        put_be(g, 0, 0xD256, 2);
        put_be(g, 2, 0x100, 2);
        put_be(g, 28, 0x6E, 4);  // content, game, header, major param and param digests are set
        put_bytes(g, 32, content_digest.data(), 32);
        put_bytes(g, 64, pfs_image_digest.data(), 32);
        put_bytes(g, 96, header_digest.data(), 32);
        // system digest: zero
        put_bytes(g, 160, major_digest.data(), 32);
        put_bytes(g, 192, param_digest.data(), 32);
    }
    // metas (sorted by id)
    {
        std::vector<Entry *> sorted;
        for (auto &e : entries) sorted.push_back(&e);
        std::stable_sort(sorted.begin(), sorted.end(), [](Entry *a, Entry *b) { return a->id < b->id; });
        Bytes m;
        for (Entry *e : sorted) { const Bytes mb = e->meta(); m.insert(m.end(), mb.begin(), mb.end()); }
        metas->data = m;
    }
    // body
    for (auto &e : entries) {
        Bytes data = e.data;
        if (e.encrypted()) {
            Bytes seed = e.meta();
            const Hash k = compute_keys(p.content_id, p.passcode, e.key_index());
            seed.insert(seed.end(), k.begin(), k.end());
            const Hash iv_key = pkgcrypto::sha256(seed);
            pkgcrypto::aes_cbc_encrypt(data.data(), data.size(), iv_key.data() + 16, iv_key.data());
        }
        memcpy(pkg.data() + e.data_offset, data.data(), data.size());
    }
    // entry digests: for each meta after the first (sorted by id), the hash of its data
    std::vector<Entry *> by_id;
    for (auto &e : entries) by_id.push_back(&e);
    std::stable_sort(by_id.begin(), by_id.end(), [](Entry *a, Entry *b) { return a->id < b->id; });
    for (size_t i = 1; i < by_id.size(); ++i) {
        const Hash h = pkgcrypto::sha256(pkg.data() + by_id[i]->data_offset, by_id[i]->data_size);
        memcpy(digests->data.data() + 32 * i, h.data(), 32);
        memcpy(pkg.data() + digests->data_offset + 32 * i, h.data(), 32);
    }
    const Hash body_digest = pkgcrypto::sha256(pkg.data() + 0x2000, size_t(body_size));
    const Hash digest_table_hash = pkgcrypto::sha256(digests->data);
    Hash sc1, sc2;
    {
        Entry *order[5];
        for (auto &e : entries) {
            if (e.id == 0x10) order[0] = &e;
            if (e.id == 0x20) order[1] = &e;
            if (e.id == 0x80) order[2] = &e;
            if (e.id == 0x100) order[3] = &e;
            if (e.id == 0x1) order[4] = &e;
        }
        pkgcrypto::Sha256 a, b;
        for (int i = 0; i < 5; ++i) a.update(pkg.data() + order[i]->data_offset, order[i]->data_size);
        for (int i = 0; i < 4; ++i) b.update(pkg.data() + order[i]->data_offset, order[i]->id == 0x100 ? 6 * 0x20 : order[i]->data_size);
        sc1.resize(32); sc2.resize(32);
        a.finish(sc1.data());
        b.finish(sc2.data());
    }
    write_header(pkg);
    put_bytes(pkg, 0x100, sc1.data(), 32);
    put_bytes(pkg, 0x120, sc2.data(), 32);
    put_bytes(pkg, 0x140, digest_table_hash.data(), 32);
    put_bytes(pkg, 0x160, body_digest.data(), 32);
    const Hash header_digest = pkgcrypto::sha256(pkg.data(), 0xFE0);
    put_bytes(pkg, 0xFE0, header_digest.data(), 32);
    const Hash header_sha = pkgcrypto::sha256(pkg.data(), 0x1000);
    const Bytes signature = rsa_encrypt_key(pkgkeys::pkg_public_keys[3], header_sha.data(), 32);
    put_bytes(pkg, 0x1000, signature.data(), 256);
    return pkg;
}

}  // namespace pkgbuild

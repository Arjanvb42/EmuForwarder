#pragma once
// The animated intro at start (about 2.5 s; X or Circle skips it). The parts come from
// icons/make_intro.py (/app0/assets/intro: bg.png, <part>.png, layout.txt) and are moved, faded,
// grown and turned here, straight into the frame buffer (0x80RRGGBB, 1920x1080).
// Needs stb_image: include it (with STB_IMAGE_IMPLEMENTATION) before this file.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <orbis/libkernel.h>
#ifndef STBI_INCLUDE_STB_IMAGE_H  // already there (with its implementation) in main.cpp
#include <stb/stb_image.h>
#endif

namespace intro {

static constexpr int W = 1920, H = 1080;

struct Sprite {
    int x = 0, y = 0, w = 0, h = 0;
    std::vector<uint8_t> rgba;  // premultiplied
};

static bool load_png(const std::string &path, int &w, int &h, std::vector<uint8_t> &rgba)
{
    int channels = 0;
    uint8_t *data = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!data)
        return false;
    rgba.assign(data, data + size_t(w) * h * 4);
    stbi_image_free(data);
    return true;
}

static inline uint32_t pack(int r, int g, int b) { return 0x80000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b); }

// Draw a sprite. alpha 0..1; scale and angle (radians) around the sprite's centre; reveal 0..1 shows
// only the left part (an arrow that grows).
static void blit(uint32_t *fb, const Sprite &s, float dx, float dy, float alpha, float scale = 1, float angle = 0,
                 float reveal = 1)
{
    if (alpha <= 0.004f || scale <= 0.01f || s.rgba.empty())
        return;
    const int a256 = int(alpha * 256);
    auto put = [&](uint32_t &out, int r, int g, int b, int a) {  // premultiplied source
        const int k = a * a256 >> 8, inv = 255 - k;
        r = r * a256 >> 8;
        g = g * a256 >> 8;
        b = b * a256 >> 8;
        const int R = (out >> 16 & 255) * inv / 255 + r, G = (out >> 8 & 255) * inv / 255 + g, B = (out & 255) * inv / 255 + b;
        out = pack(R > 255 ? 255 : R, G > 255 ? 255 : G, B > 255 ? 255 : B);
    };
    const float cx = s.x + dx + s.w / 2.0f, cy = s.y + dy + s.h / 2.0f;
    if (scale == 1 && angle == 0) {
        const int x0 = int(lroundf(s.x + dx)), y0 = int(lroundf(s.y + dy));
        const int shown = int(s.w * reveal);
        for (int y = 0; y < s.h; ++y) {
            const int sy = y0 + y;
            if (sy < 0 || sy >= H)
                continue;
            const uint8_t *src = &s.rgba[size_t(y) * s.w * 4];
            uint32_t *row = fb + size_t(sy) * W;
            for (int x = 0; x < shown; ++x) {
                const int sx = x0 + x, a = src[x * 4 + 3];
                if (a && sx >= 0 && sx < W)
                    put(row[sx], src[x * 4], src[x * 4 + 1], src[x * 4 + 2], a);
            }
        }
        return;
    }
    // Scaled and/or turned: map every screen pixel back into the sprite (bilinear).
    const float c = cosf(angle), sn = sinf(angle);
    const float half = 0.5f * scale * sqrtf(float(s.w * s.w + s.h * s.h));
    const int bx0 = int(fmaxf(0, cx - half)), bx1 = int(fminf(W - 1, cx + half));
    const int by0 = int(fmaxf(0, cy - half)), by1 = int(fminf(H - 1, cy + half));
    for (int y = by0; y <= by1; ++y) {
        uint32_t *row = fb + size_t(y) * W;
        for (int x = bx0; x <= bx1; ++x) {
            const float px = (x + 0.5f - cx) / scale, py = (y + 0.5f - cy) / scale;
            const float u = c * px + sn * py + s.w / 2.0f - 0.5f, v = -sn * px + c * py + s.h / 2.0f - 0.5f;
            if (u < 0 || v < 0 || u >= s.w - 1 || v >= s.h - 1)
                continue;
            const int iu = int(u), iv = int(v);
            const float fu = u - iu, fv = v - iv;
            const uint8_t *p = &s.rgba[(size_t(iv) * s.w + iu) * 4];
            const uint8_t *q = p + size_t(s.w) * 4;
            int ch[4];
            for (int k = 0; k < 4; ++k)
                ch[k] = int((p[k] * (1 - fu) + p[4 + k] * fu) * (1 - fv) + (q[k] * (1 - fu) + q[4 + k] * fu) * fv);
            if (ch[3])
                put(row[x], ch[0], ch[1], ch[2], ch[3]);
        }
    }
}

static float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static float phase(float t, float from, float to) { return clamp01((t - from) / (to - from)); }
static float ease_out(float t) { return 1 - powf(1 - t, 3); }
static float ease_back(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1;
    return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

// Plays the intro of "emuforwarder" or "efupdater". frame_buffer() gives the buffer to draw in,
// flip() shows it and waits for the next frame, skip() is true when X or Circle was pressed.
static bool play(const char *app, const std::function<uint32_t *()> &frame_buffer, const std::function<void()> &flip,
                 const std::function<bool()> &skip)
{
    const std::string folder = std::string("/app0/assets/intro/") + app + "/";
    int bw = 0, bh = 0;
    std::vector<uint8_t> bg_rgba;
    if (!load_png(folder + "bg.png", bw, bh, bg_rgba) || bw != W || bh != H)
        return false;
    std::vector<uint32_t> bg(size_t(W) * H);
    for (size_t i = 0; i < bg.size(); ++i)
        bg[i] = pack(bg_rgba[i * 4], bg_rgba[i * 4 + 1], bg_rgba[i * 4 + 2]);
    bg_rgba.clear();
    bg_rgba.shrink_to_fit();

    std::map<std::string, Sprite> parts;
    if (FILE *layout = fopen((folder + "layout.txt").c_str(), "r")) {
        char name[32];
        Sprite s;
        while (fscanf(layout, "%31s %d %d %d %d", name, &s.x, &s.y, &s.w, &s.h) == 5) {
            int w = 0, h = 0;
            if (!load_png(folder + name + ".png", w, h, s.rgba) || w != s.w || h != s.h)
                continue;
            for (size_t i = 0; i < s.rgba.size(); i += 4)  // premultiply
                for (int k = 0; k < 3; ++k)
                    s.rgba[i + k] = uint8_t(s.rgba[i + k] * s.rgba[i + 3] / 255);
            parts[name] = s;
        }
        fclose(layout);
    }
    const bool forwarder = !strcmp(app, "emuforwarder");
    const float total = forwarder ? 2.9f : 2.7f, out_from = total - 0.3f;
    const uint64_t start = sceKernelGetProcessTime();
    for (;;) {
        const float t = (sceKernelGetProcessTime() - start) / 1e6f;
        if (t >= total || skip())
            break;
        uint32_t *fb = frame_buffer();
        // background fades in from black, everything fades out at the end
        const float in = ease_out(phase(t, 0, 0.35f)), out = 1 - phase(t, out_from, total);
        const float level = in * out;
        if (level >= 0.999f) {
            memcpy(fb, bg.data(), bg.size() * 4);
        } else {
            const int k = int(level * 256);
            for (size_t i = 0; i < bg.size(); ++i) {
                const uint32_t v = bg[i];
                fb[i] = pack((v >> 16 & 255) * k >> 8, (v >> 8 & 255) * k >> 8, (v & 255) * k >> 8);
            }
        }
        auto part = [&](const char *name) -> const Sprite & { return parts[name]; };
        if (forwarder) {
            float p = ease_out(phase(t, 0.15f, 0.65f));
            blit(fb, part("card"), -90 * (1 - p), 0, p * out);
            p = ease_out(phase(t, 0.35f, 0.85f));
            blit(fb, part("panel"), 90 * (1 - p), 0, p * out);
            p = ease_out(phase(t, 0.75f, 1.15f));
            blit(fb, part("arrow"), 0, 0, (p > 0) * out, 1, 0, p);
            p = phase(t, 1.05f, 1.4f);
            blit(fb, part("lit"), 0, 0, clamp01(p * 2) * out, p > 0 ? 0.6f + 0.4f * ease_back(p) : 0);
        } else {
            float p = ease_out(phase(t, 0.1f, 1.6f));
            blit(fb, part("ring"), 0, 0, ease_out(phase(t, 0.1f, 0.5f)) * out, 1, float(-1.5 * M_PI) * (1 - p));
            p = phase(t, 0.5f, 0.9f);
            blit(fb, part("invader"), 0, 0, clamp01(p * 1.5f) * out, p > 0 ? 0.7f + 0.3f * ease_back(p) : 0);
        }
        const float title_from = forwarder ? 1.2f : 1.0f;
        float p = ease_out(phase(t, title_from, title_from + 0.5f));
        blit(fb, part("title"), 0, 30 * (1 - p), p * out);
        p = ease_out(phase(t, title_from + 0.25f, title_from + 0.7f));
        blit(fb, part("subtitle"), 0, 0, p * out);
        flip();
    }
    return true;
}

}  // namespace intro

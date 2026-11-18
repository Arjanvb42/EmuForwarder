#pragma once
// Background music: plays the .ogg and .wav files in /data/PS4Tegelmaker/muziek/ in a loop, on its
// own thread. No music ships with the app; put your own files there (FTP).

#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <string>
#include <vector>

#include <orbis/AudioOut.h>
#include <orbis/UserService.h>

#define STB_VORBIS_HEADER_ONLY
extern "C" {
#include <stb/stb_vorbis.c>
}

namespace music {

static constexpr char Folder[] = "/data/PS4Tegelmaker/muziek";
static constexpr int Rate = 48000, Block = 1024;

static volatile bool enabled = true;
static volatile int volume = 60;  // percent
static volatile bool running = false;
static int track_count = 0;

static std::vector<std::string> tracks()
{
    std::vector<std::string> found;
    DIR *dir = opendir(Folder);
    if (!dir)
        return found;
    while (dirent *entry = readdir(dir)) {
        const char *dot = strrchr(entry->d_name, '.');
        if (dot && (!strcasecmp(dot, ".ogg") || !strcasecmp(dot, ".wav")))
            found.push_back(std::string(Folder) + "/" + entry->d_name);
    }
    closedir(dir);
    std::sort(found.begin(), found.end());
    return found;
}

// A source of stereo 16-bit samples at its own rate.
struct Source {
    stb_vorbis *ogg = nullptr;
    FILE *wav = nullptr;
    int rate = 0, channels = 0;
    long wav_left = 0;  // bytes of sample data left

    bool open(const std::string &path)
    {
        if (strcasecmp(path.c_str() + path.size() - 4, ".ogg") == 0) {
            int error = 0;
            ogg = stb_vorbis_open_filename(path.c_str(), &error, nullptr);
            if (!ogg)
                return false;
            const stb_vorbis_info info = stb_vorbis_get_info(ogg);
            rate = int(info.sample_rate);
            channels = info.channels;
            return rate > 0;
        }
        // WAV: 16-bit PCM only.
        wav = fopen(path.c_str(), "rb");
        if (!wav)
            return false;
        uint8_t header[12];
        if (fread(header, 1, 12, wav) != 12 || memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4))
            return false;
        int bits = 0;
        for (;;) {
            uint8_t chunk[8];
            if (fread(chunk, 1, 8, wav) != 8)
                return false;
            const uint32_t size = chunk[4] | chunk[5] << 8 | chunk[6] << 16 | uint32_t(chunk[7]) << 24;
            if (!memcmp(chunk, "fmt ", 4)) {
                uint8_t fmt[16];
                if (size < 16 || fread(fmt, 1, 16, wav) != 16)
                    return false;
                channels = fmt[2] | fmt[3] << 8;
                rate = int(fmt[4] | fmt[5] << 8 | fmt[6] << 16 | uint32_t(fmt[7]) << 24);
                bits = fmt[14] | fmt[15] << 8;
                fseek(wav, long(size - 16 + (size & 1)), SEEK_CUR);
            } else if (!memcmp(chunk, "data", 4)) {
                wav_left = long(size);
                return bits == 16 && rate > 0 && channels >= 1 && channels <= 2;
            } else {
                fseek(wav, long(size + (size & 1)), SEEK_CUR);
            }
        }
    }

    // Up to frames stereo frames; 0 at the end.
    int read(int16_t *out, int frames)
    {
        if (ogg)
            return stb_vorbis_get_samples_short_interleaved(ogg, 2, out, frames * 2);
        const int frame_bytes = 2 * channels;
        frames = int(std::min<long>(frames, wav_left / frame_bytes));
        if (frames <= 0)
            return 0;
        int16_t buffer[Block * 2];
        frames = std::min(frames, Block);
        const int got = int(fread(buffer, frame_bytes, size_t(frames), wav));
        wav_left -= long(got) * frame_bytes;
        for (int i = 0; i < got; ++i) {
            out[2 * i] = buffer[i * channels];
            out[2 * i + 1] = buffer[i * channels + channels - 1];
        }
        return got;
    }

    void close()
    {
        if (ogg)
            stb_vorbis_close(ogg);
        if (wav)
            fclose(wav);
        ogg = nullptr;
        wav = nullptr;
    }
};

static void *play_thread(void *)
{
    const int port = sceAudioOutOpen(ORBIS_USER_SERVICE_USER_ID_SYSTEM, ORBIS_AUDIO_OUT_PORT_TYPE_MAIN, 0, Block, Rate,
                                     ORBIS_AUDIO_OUT_PARAM_FORMAT_S16_STEREO);
    if (port <= 0) {
        sys::log("muziek: geluid openen mislukt (0x%08x)", unsigned(port));
        running = false;
        return nullptr;
    }
    static int16_t output[Block * 2];
    static int16_t input[Block * 2 + 4];
    for (;;) {
        const std::vector<std::string> list = tracks();
        track_count = int(list.size());
        if (list.empty()) {
            sleep(5);  // files may be added later
            continue;
        }
        for (const std::string &path : list) {
            Source source;
            if (!source.open(path)) {
                sys::log("muziek: %s niet af te spelen (ogg, of wav 16-bit)", path.c_str());
                source.close();
                continue;
            }
            // Linear resampling from the file's rate to 48 kHz.
            const double step = double(source.rate) / Rate;
            double position = 0;
            int have = source.read(input, Block);
            bool ended = have <= 0;
            while (!ended) {
                while (!enabled)
                    usleep(100 * 1000);
                const int gain = volume * 256 / 100;
                for (int i = 0; i < Block; ++i) {
                    int at = int(position);
                    while (at + 1 >= have && !ended) {
                        // Keep the last frame, read more after it.
                        input[0] = input[2 * (have - 1)];
                        input[1] = input[2 * (have - 1) + 1];
                        const int more = source.read(input + 2, Block);
                        position -= have - 1;
                        at = int(position);
                        if (more <= 0)
                            ended = true;
                        have = 1 + std::max(more, 0);
                    }
                    if (ended && at + 1 >= have) {
                        memset(output + 2 * i, 0, size_t(Block - i) * 4);
                        break;
                    }
                    const double t = position - at;
                    for (int c = 0; c < 2; ++c) {
                        const double s = input[2 * at + c] * (1 - t) + input[2 * (at + 1) + c] * t;
                        output[2 * i + c] = int16_t(std::max(-32768.0, std::min(32767.0, s * gain / 256)));
                    }
                    position += step;
                }
                sceAudioOutOutput(port, output);
            }
            source.close();
        }
    }
    return nullptr;
}

static void start()
{
    if (running)
        return;
    running = true;
    sceAudioOutInit();
    pthread_t thread;
    if (pthread_create(&thread, nullptr, play_thread, nullptr) == 0)
        pthread_detach(thread);
    else
        running = false;
}

}  // namespace music

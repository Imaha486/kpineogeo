#pragma once
#include <memory>
#include <vector>
#include "songscan.h"

struct NeoTrack
{
    uint8_t code;
    bool loop;
    uint32_t lenMs;   // for non-loop tracks: playback length incl. tail
};

enum NeoListMode { NEO_LIST_MUSIC = 0, NEO_LIST_ALL = 1 };

std::vector<NeoTrack> NeoMakeTrackList(const std::vector<NeoSong>& songs, int mode, bool dedupe);

class NeoPlayer
{
public:
    NeoPlayer(const NeoRomSet* roms, const NeoDriverProfile& prof, uint32_t outRate, double gain);
    void start(const NeoTrack& t);
    // interleaved stereo float. returns frames written (< frames at end of a non-loop track)
    uint32_t render(float* out, uint32_t frames);
    uint64_t seek(uint64_t posFrames);
    uint32_t outRate() const { return m_outRate; }

private:
    void refill();
    const NeoRomSet* m_roms;
    NeoDriverProfile m_prof;
    std::unique_ptr<NeoSound> m_ns;
    NeoTrack m_track{};
    uint32_t m_outRate;
    float m_gain;
    double m_step;           // native samples per output sample
    double m_phase = 0;      // position between hist[1] and hist[2]
    float m_hist[4][2] = {}; // 4-point cubic history
    std::vector<int32_t> m_buf;
    size_t m_bufPos = 0, m_bufLen = 0;
    uint64_t m_pos = 0, m_end = 0;
};

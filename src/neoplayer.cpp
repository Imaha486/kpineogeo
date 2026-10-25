#include "neoplayer.h"
#include <algorithm>
#include <cstring>
#include <set>

static constexpr double NATIVE_RATE = double(NeoSound::YM_CLOCK) / 144.0;
static constexpr uint32_t TAIL_MS = 1500;

std::vector<NeoTrack> NeoMakeTrackList(const std::vector<NeoSong>& songs, int mode, bool dedupe)
{
    std::vector<NeoTrack> out;
    std::set<uint32_t> seen;
    for (const NeoSong& s : songs)
    {
        bool use;
        if (mode == NEO_LIST_ALL)
            use = s.keyOns >= 2 || s.lenMs >= 50;
        else
            use = s.loop || s.lenMs >= 2500 || (s.lenMs >= 2000 && s.keyOns >= 20);
        if (!use) continue;
        if (dedupe && !seen.insert(s.hash).second) continue;
        out.push_back({ s.code, s.loop, s.loop ? 0 : s.lenMs + TAIL_MS });
    }
    return out;
}

NeoPlayer::NeoPlayer(const NeoRomSet* roms, const NeoDriverProfile& prof, uint32_t outRate, double gain)
    : m_roms(roms), m_prof(prof), m_ns(new NeoSound()), m_outRate(outRate), m_gain(float(gain / 32768.0))
{
    m_step = NATIVE_RATE / double(m_outRate);
    m_buf.resize(2048 * 2);
}

void NeoPlayer::start(const NeoTrack& t)
{
    m_track = t;
    m_ns.reset(new NeoSound());   // fresh chip: ymfm reset() keeps some internal counters
    NeoBoot(*m_ns, *m_roms, m_prof);
    NeoStartSong(*m_ns, m_prof, t.code);
    m_bufPos = m_bufLen = 0;
    std::memset(m_hist, 0, sizeof(m_hist));
    m_phase = 0;
    m_pos = 0;
    m_end = t.loop ? UINT64_MAX : uint64_t(t.lenMs) * m_outRate / 1000;
}

void NeoPlayer::refill()
{
    const uint32_t n = uint32_t(m_buf.size() / 2);
    m_ns->generate(m_buf.data(), n);
    m_bufPos = 0;
    m_bufLen = n;
}

uint32_t NeoPlayer::render(float* out, uint32_t frames)
{
    uint32_t done = 0;
    while (done < frames && m_pos < m_end)
    {
        // advance input until phase is within [0,1)
        while (m_phase >= 1.0)
        {
            if (m_bufPos >= m_bufLen) refill();
            std::memmove(m_hist[0], m_hist[1], sizeof(float) * 6);
            m_hist[3][0] = float(m_buf[m_bufPos * 2 + 0]);
            m_hist[3][1] = float(m_buf[m_bufPos * 2 + 1]);
            m_bufPos++;
            m_phase -= 1.0;
        }
        const float x = float(m_phase);
        for (int c = 0; c < 2; c++)
        {
            // Catmull-Rom between hist[1] and hist[2]
            const float y0 = m_hist[0][c], y1 = m_hist[1][c], y2 = m_hist[2][c], y3 = m_hist[3][c];
            const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
            const float b = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            const float cc = -0.5f * y0 + 0.5f * y2;
            out[done * 2 + c] = (((a * x + b) * x + cc) * x + y1) * m_gain;
        }
        m_phase += m_step;
        done++;
        m_pos++;
    }
    return done;
}

uint64_t NeoPlayer::seek(uint64_t pos)
{
    start(m_track);
    if (pos > m_end) pos = m_end;
    // fast-forward the emulation (synthesis is kept running so ADPCM state stays exact)
    // normal playback lags 3 native samples behind (cubic history), reproduce that exactly
    const double target = double(pos) * m_step;
    const uint64_t native = uint64_t(target);
    const uint64_t skipN = native >= 3 ? native - 3 : 0;
    uint64_t left = skipN;
    while (left > 0)
    {
        const uint32_t n = uint32_t(std::min<uint64_t>(left, 65536));
        m_ns->skip(n);
        left -= n;
    }
    m_pos = pos;
    m_phase = target - double(skipN);
    return pos;
}

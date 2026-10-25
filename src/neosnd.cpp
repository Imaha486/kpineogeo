// NEOGEO sound board emulation (Z80 + YM2610)
#include "neosnd.h"
#include <cstring>

NeoSound::NeoSound() : m_ym(*this)
{
    m_ym.set_fidelity(ymfm::OPN_FIDELITY_MED);   // output = 8MHz/144 = 55.555kHz
}

void NeoSound::setRoms(const std::vector<uint8_t>* m1,
                       const std::vector<uint8_t>* adpcmA,
                       const std::vector<uint8_t>* adpcmB)
{
    m_m1 = m1; m_va = adpcmA; m_vb = adpcmB;
}

void NeoSound::reset()
{
    z80_init(&m_cpu);
    m_cpu.read_byte = memRead;
    m_cpu.write_byte = memWrite;
    m_cpu.port_in = portIn;
    m_cpu.port_out = portOut;
    m_cpu.userdata = this;
    std::memset(m_ram, 0, sizeof(m_ram));
    // linear mapping at power-on (same as MAME)
    m_bank[0] = 0x1e * 0x800;   // F000-F7FF
    m_bank[1] = 0x0e * 0x1000;  // E000-EFFF
    m_bank[2] = 0x06 * 0x2000;  // C000-DFFF
    m_bank[3] = 0x02 * 0x4000;  // 8000-BFFF
    m_cmd = 0; m_reply = 0;
    m_nmiEnabled = false; m_irq = false;
    m_timer[0] = m_timer[1] = -1;
    m_cycDebt = 0;
    m_ymAddr[0] = m_ymAddr[1] = 0;
    m_samples = m_lastKeyOn = 0; m_keyOns = 0;
    m_ym.reset();
}

void NeoSound::sendCommand(uint8_t code)
{
    m_cmd = code;
    if (m_nmiEnabled)
        z80_gen_nmi(&m_cpu);
}

uint8_t NeoSound::memRead(void* ud, uint16_t a)
{
    NeoSound* s = static_cast<NeoSound*>(ud);
    const std::vector<uint8_t>& rom = *s->m_m1;
    uint32_t off;
    if (a < 0x8000)       off = a;
    else if (a < 0xc000)  off = s->m_bank[3] + (a & 0x3fff);
    else if (a < 0xe000)  off = s->m_bank[2] + (a & 0x1fff);
    else if (a < 0xf000)  off = s->m_bank[1] + (a & 0x0fff);
    else if (a < 0xf800)  off = s->m_bank[0] + (a & 0x07ff);
    else return s->m_ram[a & 0x7ff];
    return rom.empty() ? 0xff : rom[off % rom.size()];
}

void NeoSound::memWrite(void* ud, uint16_t a, uint8_t v)
{
    if (a >= 0xf800)
        static_cast<NeoSound*>(ud)->m_ram[a & 0x7ff] = v;
}

uint8_t NeoSound::portIn(z80* z, uint16_t port)
{
    NeoSound* s = static_cast<NeoSound*>(z->userdata);
    const uint8_t hi = port >> 8;
    switch (port & 0x0f)
    {
    case 0x00:
        // reading the latch also clears the pending NMI on real hardware
        return s->m_cmd;
    case 0x04: case 0x05: case 0x06: case 0x07:
        return s->m_ym.read(port & 3);
    case 0x08: s->m_bank[0] = uint32_t(hi) * 0x800;  return 0;
    case 0x09: s->m_bank[1] = uint32_t(hi) * 0x1000; return 0;
    case 0x0a: s->m_bank[2] = uint32_t(hi) * 0x2000; return 0;
    case 0x0b: s->m_bank[3] = uint32_t(hi) * 0x4000; return 0;
    default:
        return 0;
    }
}

void NeoSound::portOut(z80* z, uint16_t port, uint8_t v)
{
    NeoSound* s = static_cast<NeoSound*>(z->userdata);
    switch (port & 0x0f)
    {
    case 0x04: case 0x06:
        s->m_ymAddr[(port >> 1) & 1] = v;
        s->m_ym.write(port & 3, v);
        break;
    case 0x05: case 0x07:
        s->noteWrite((port >> 1) & 1, s->m_ymAddr[(port >> 1) & 1], v);
        s->m_ym.write(port & 3, v);
        break;
    case 0x08:
        s->m_nmiEnabled = (port & 0x10) == 0;
        break;
    case 0x0c:
        s->m_reply = v;
        break;
    default:
        break;
    }
}

void NeoSound::noteWrite(int hi, uint8_t reg, uint8_t v)
{
    m_hash = (m_hash ^ (uint32_t(hi) << 16 | uint32_t(reg) << 8 | v)) * 16777619u;
    bool on = false;
    if (!hi)
    {
        if (reg == 0x28 && (v & 0xf0)) on = true;                 // FM key on
        else if (reg == 0x10 && (v & 0x80)) on = true;            // ADPCM-B start
        else if (reg >= 0x08 && reg <= 0x0a && (v & 0x1f)) on = true; // SSG volume
    }
    else if (reg == 0x00 && !(v & 0x80) && (v & 0x3f)) on = true; // ADPCM-A key on
    if (on) { m_lastKeyOn = m_samples; m_keyOns++; }
}

void NeoSound::ymfm_set_timer(uint32_t tnum, int32_t duration)
{
    if (tnum > 1) return;
    if (duration < 0) { m_timer[tnum] = -1; return; }
    m_timer[tnum] = duration;
}

uint8_t NeoSound::ymfm_external_read(ymfm::access_class type, uint32_t address)
{
    const std::vector<uint8_t>* v = (type == ymfm::ACCESS_ADPCM_B) ? m_vb : m_va;
    if (!v || v->empty()) return 0;
    return address < v->size() ? (*v)[address] : 0;
}

// advance by one native sample period (144 YM clocks = 72 Z80 cycles)
inline void NeoSound::step1()
{
    m_samples++;
    m_cycDebt += Z80_CLOCK * 144 / YM_CLOCK;   // 72
    const unsigned long start = m_cpu.cyc;
    while ((int64_t)(m_cpu.cyc - start) < m_cycDebt)
    {
        m_cpu.int_pending = m_irq;   // level triggered /INT
        z80_step(&m_cpu);
    }
    m_cycDebt -= (int64_t)(m_cpu.cyc - start);

    for (int t = 0; t < 2; t++)
    {
        if (m_timer[t] < 0) continue;
        m_timer[t] -= 144;
        if (m_timer[t] <= 0)
        {
            const int64_t over = m_timer[t];
            m_timer[t] = -1;
            m_engine->engine_timer_expired(t);   // may re-arm via ymfm_set_timer
            if (m_timer[t] >= 0)
            {
                m_timer[t] += over;
                if (m_timer[t] < 1) m_timer[t] = 1;
            }
        }
    }
}

void NeoSound::generate(int32_t* out, uint32_t frames)
{
    ymfm::ym2610::output_data od;
    for (uint32_t i = 0; i < frames; i++)
    {
        step1();
        m_ym.generate(&od, 1);
        // FM+ADPCM (stereo) and SSG (mono). Balance roughly as on the real board.
        const int32_t ssg = od.data[2] * 28 / 98;
        out[i * 2 + 0] = od.data[0] + ssg;
        out[i * 2 + 1] = od.data[1] + ssg;
    }
}

void NeoSound::skip(uint32_t frames)
{
    ymfm::ym2610::output_data od;
    for (uint32_t i = 0; i < frames; i++)
    {
        step1();
        m_ym.generate(&od, 1);
    }
}

void NeoSound::runFast(uint32_t frames)
{
    for (uint32_t i = 0; i < frames; i++)
        step1();
}

void NeoSound::save(State& s)
{
    s.cpu = m_cpu;
    std::memcpy(s.ram, m_ram, sizeof(m_ram));
    std::memcpy(s.bank, m_bank, sizeof(m_bank));
    s.cmd = m_cmd; s.reply = m_reply;
    s.nmiEnabled = m_nmiEnabled; s.irq = m_irq;
    s.timer[0] = m_timer[0]; s.timer[1] = m_timer[1];
    s.cycDebt = m_cycDebt;
    s.ymAddr[0] = m_ymAddr[0]; s.ymAddr[1] = m_ymAddr[1];
    ymfm::ymfm_saved_state st(s.ym, true);
    m_ym.save_restore(st);
}

void NeoSound::load(const State& s)
{
    m_cpu = s.cpu;
    m_cpu.userdata = this;
    std::memcpy(m_ram, s.ram, sizeof(m_ram));
    std::memcpy(m_bank, s.bank, sizeof(m_bank));
    m_cmd = s.cmd; m_reply = s.reply;
    m_nmiEnabled = s.nmiEnabled; m_irq = s.irq;
    m_timer[0] = s.timer[0]; m_timer[1] = s.timer[1];
    m_cycDebt = s.cycDebt;
    m_ymAddr[0] = s.ymAddr[0]; m_ymAddr[1] = s.ymAddr[1];
    std::vector<uint8_t> tmp = s.ym;
    ymfm::ymfm_saved_state st(tmp, false);
    m_ym.save_restore(st);
}

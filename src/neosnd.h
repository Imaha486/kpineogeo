// NEOGEO sound board emulation (Z80 + YM2610)
#pragma once
#include <cstdint>
#include <vector>
#include "ymfm_opn.h"
extern "C" {
#include "z80.h"
}

class NeoSound : public ymfm::ymfm_interface
{
public:
    static constexpr uint32_t YM_CLOCK  = 8000000;
    static constexpr uint32_t Z80_CLOCK = 4000000;
    static constexpr uint32_t NATIVE_RATE_NUM = YM_CLOCK; // native rate = 8MHz/144
    static constexpr uint32_t NATIVE_RATE_DEN = 144;

    NeoSound();
    ~NeoSound() override = default;

    // m1: Z80 program, adpcmA / adpcmB: V ROM images (B may equal A)
    void setRoms(const std::vector<uint8_t>* m1,
                 const std::vector<uint8_t>* adpcmA,
                 const std::vector<uint8_t>* adpcmB);

    void reset();
    // 68000 -> Z80 sound command
    void sendCommand(uint8_t code);
    uint8_t reply() const { return m_reply; }
    // generate native-rate stereo samples (interleaved L/R int32, unclipped)
    void generate(int32_t* out, uint32_t frames);
    // just run without producing output (fast-forward)
    void skip(uint32_t frames);
    // run Z80 + timers only (no synthesis) - for song scanning
    void runFast(uint32_t frames);
    // sound activity tracking (key-on events); counted in native samples
    uint64_t sampleCount() const { return m_samples; }
    uint64_t lastKeyOn() const { return m_lastKeyOn; }
    uint32_t keyOnCount() const { return m_keyOns; }
    uint32_t writeHash() const { return m_hash; }
    void clearActivity() { m_lastKeyOn = 0; m_keyOns = 0; m_hash = 2166136261u; }

    // snapshot (Z80 + RAM + banks + YM state)
    struct State {
        z80 cpu;
        uint8_t ram[0x800];
        uint32_t bank[4];
        uint8_t cmd, reply;
        bool nmiEnabled, irq;
        int64_t timer[2];
        int64_t cycDebt;
    uint8_t ymAddr[2];
        std::vector<uint8_t> ym;
    };
    void save(State& s);
    void load(const State& s);

    // ymfm_interface
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
    void ymfm_update_irq(bool asserted) override { m_irq = asserted; }
    uint8_t ymfm_external_read(ymfm::access_class type, uint32_t address) override;

private:
    static uint8_t memRead(void* ud, uint16_t addr);
    static void memWrite(void* ud, uint16_t addr, uint8_t v);
    static uint8_t portIn(z80* z, uint16_t port);
    static void portOut(z80* z, uint16_t port, uint8_t v);
    inline void step1();

    const std::vector<uint8_t>* m_m1 = nullptr;
    const std::vector<uint8_t>* m_va = nullptr;
    const std::vector<uint8_t>* m_vb = nullptr;
    z80 m_cpu{};
    uint8_t m_ram[0x800]{};
    uint32_t m_bank[4]{};      // byte offsets for F000/E000/C000/8000 windows
    uint8_t m_cmd = 0, m_reply = 0;
    bool m_nmiEnabled = false, m_irq = false;
    int64_t m_timer[2] = { -1, -1 };  // remaining YM clocks, -1 = stopped
    int64_t m_cycDebt = 0;
    uint8_t m_ymAddr[2] = { 0, 0 };
    uint64_t m_samples = 0, m_lastKeyOn = 0;
    uint32_t m_keyOns = 0;
    uint32_t m_hash = 2166136261u;
    void noteWrite(int hi, uint8_t reg, uint8_t v);
    ymfm::ym2610 m_ym;
};

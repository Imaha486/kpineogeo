// Linux test harness: rip.exe m1 v1 [v2..] -- code seconds out.wav
#include "neosnd.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>

static std::vector<uint8_t> readf(const char* p)
{
    std::vector<uint8_t> v; FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); }
    fseek(f, 0, SEEK_END); v.resize(ftell(f)); fseek(f, 0, SEEK_SET);
    if (fread(v.data(), 1, v.size(), f) != v.size()) exit(1);
    fclose(f); return v;
}

int main(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: m1 v1 v2 code sec [out.wav] [pre]\n"); return 1; }
    auto m1 = readf(argv[1]);
    auto v = readf(argv[2]); auto v2 = readf(argv[3]); v.insert(v.end(), v2.begin(), v2.end());
    int code = strtol(argv[4], 0, 0); double sec = atof(argv[5]);
    const char* pre = argc > 7 ? argv[7] : "";
    NeoSound* ns = new NeoSound();
    ns->setRoms(&m1, &v, &v);
    ns->reset();
    const int rate = 55555;
    ns->skip(rate / 2);
    for (const char* p = pre; *p; ) { int c = strtol(p, (char**)&p, 0); ns->sendCommand(c); ns->skip(rate/10); if (*p==',') p++; }
    ns->sendCommand(code);
    int n = int(sec * rate);
    std::vector<int32_t> buf(n * 2);
    ns->generate(buf.data(), n);
    std::vector<int16_t> pcm(n * 2);
    for (int s = 0; s < (int)sec; s++) {
        double e = 0; for (int i = s * rate; i < (s + 1) * rate; i++) e += double(buf[i*2]) * buf[i*2];
        printf("%.1f ", sqrt(e / rate));
    }
    printf(" reply=%02x\n", ns->reply());
    for (int i = 0; i < n * 2; i++) { int x = buf[i]; pcm[i] = x > 32767 ? 32767 : x < -32768 ? -32768 : x; }
    if (argc > 6 && argv[6][0]) {
        FILE* f = fopen(argv[6], "wb");
        uint32_t ds = n * 4, r = rate, br = rate * 4;
        fwrite("RIFF", 1, 4, f); uint32_t t = 36 + ds; fwrite(&t, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
        t = 16; fwrite(&t, 4, 1, f); uint16_t s16 = 1; fwrite(&s16, 2, 1, f); s16 = 2; fwrite(&s16, 2, 1, f);
        fwrite(&r, 4, 1, f); fwrite(&br, 4, 1, f); s16 = 4; fwrite(&s16, 2, 1, f); s16 = 16; fwrite(&s16, 2, 1, f);
        fwrite("data", 1, 4, f); fwrite(&ds, 4, 1, f); fwrite(pcm.data(), 2, n * 2, f); fclose(f);
    }
}


/* Copyright (c) 2020, Peter Barrett
**
** Permission to use, copy, modify, and/or distribute this software for
** any purpose with or without fee is hereby granted, provided that the
** above copyright notice and this permission notice appear in all copies.
**
** THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
** WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR
** BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES
** OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
** WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
** ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
** SOFTWARE.
*/

#include "emu.h"
#include "media.h"
#include "esp_timer.h"

// Number of extra NES frames emulated (without drawing) per screen refresh, chosen automatically
// so the game keeps its normal speed when one full frame takes longer than 1/60 s.
int g_nes_skip = 0;
int g_nes_frames = 0;   // frames emulated by the last update (read by esp_8_bit.ino for pacing)

extern "C" {
#include "nofrendo/osd.h"
#include "nofrendo/event.h"
};
#include "math.h"

using namespace std;

// https://wiki.nesdev.com/w/index.php/NTSC_video
// NES/SMS have pixel rates of 5.3693175, or 2/3 color clock
// in 3 phase mode each pixel gets 2 DAC values written, 2 color clocks = 3 nes pixels

uint32_t nes_3_phase[64] = {
    0x2C2C2C00,0x241D2400,0x221D2600,0x1F1F2700,0x1D222600,0x1D242400,0x1D262200,0x1F271F00,
    0x22261D00,0x24241D00,0x26221D00,0x271F1F00,0x261D2200,0x14141400,0x14141400,0x14141400,
    0x38383800,0x2C252C00,0x2A252E00,0x27272F00,0x252A2E00,0x252C2C00,0x252E2A00,0x272F2700,
    0x2A2E2500,0x2C2C2500,0x2E2A2500,0x2F272700,0x2E252A00,0x1F1F1F00,0x15151500,0x15151500,
    0x45454500,0x3A323A00,0x37333C00,0x35353C00,0x33373C00,0x323A3A00,0x333C3700,0x353C3500,
    0x373C3300,0x3A3A3200,0x3C373300,0x3C353500,0x3C333700,0x2B2B2B00,0x16161600,0x16161600,
    0x45454500,0x423B4200,0x403B4400,0x3D3D4500,0x3B404400,0x3B424200,0x3B444000,0x3D453D00,
    0x40443B00,0x42423B00,0x44403B00,0x453D3D00,0x443B4000,0x39393900,0x17171700,0x17171700,
};
uint32_t nes_4_phase[64] = {
    0x2C2C2C2C,0x241D1F26,0x221D2227,0x1F1D2426,0x1D1F2624,0x1D222722,0x1D24261F,0x1F26241D,
    0x2227221D,0x24261F1D,0x26241D1F,0x27221D22,0x261F1D24,0x14141414,0x14141414,0x14141414,
    0x38383838,0x2C25272E,0x2A252A2F,0x27252C2E,0x25272E2C,0x252A2F2A,0x252C2E27,0x272E2C25,
    0x2A2F2A25,0x2C2E2725,0x2E2C2527,0x2F2A252A,0x2E27252C,0x1F1F1F1F,0x15151515,0x15151515,
    0x45454545,0x3A33353C,0x3732373C,0x35333A3C,0x33353C3A,0x32373C37,0x333A3C35,0x353C3A33,
    0x373C3732,0x3A3C3533,0x3C3A3335,0x3C373237,0x3C35333A,0x2B2B2B2B,0x16161616,0x16161616,
    0x45454545,0x423B3D44,0x403B4045,0x3D3B4244,0x3B3D4442,0x3B404540,0x3B42443D,0x3D44423B,
    0x4045403B,0x42443D3B,0x44423B3D,0x45403B40,0x443D3B42,0x39393939,0x17171717,0x17171717,
};


// PAL yuyv table, must be in RAM
uint32_t _nes_yuv_4_phase_pal[] = {
    0x31313131,0x2D21202B,0x2720252D,0x21212B2C,0x1D23302A,0x1B263127,0x1C293023,0x202B2D22,
    0x262B2722,0x2C2B2122,0x2F2B1E23,0x31291F27,0x30251F2A,0x18181818,0x19191919,0x19191919,
    0x3D3D3D3D,0x34292833,0x2F282D34,0x29283334,0x252B3732,0x232E392E,0x2431382B,0x28333429,
    0x2D342F28,0x33342928,0x3732252A,0x392E232E,0x382B2431,0x24242424,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x42373540,0x3C373B40,0x36374040,0x3337433F,0x3139433B,0x323D4338,0x35414237,
    0x3B423D35,0x41413736,0x453F3238,0x473C313B,0x4639323F,0x2F2F2F2F,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x48413D45,0x42404345,0x3D3F4644,0x3B3D4543,0x3B3E4542,0x3B42453F,0x3E47463E,
    0x434A453E,0x46483E3D,0x4843393E,0x4A403842,0x4B403944,0x3E3E3E3E,0x1B1B1B1B,0x1B1B1B1B,
    //odd
    0x31313131,0x20212D2B,0x2520272D,0x2B21212C,0x30231D2A,0x31261B27,0x30291C23,0x2D2B2022,
    0x272B2622,0x212B2C22,0x1E2B2F23,0x1F293127,0x1F25302A,0x18181818,0x19191919,0x19191919,
    0x3D3D3D3D,0x28293433,0x2D282F34,0x33282934,0x372B2532,0x392E232E,0x3831242B,0x34332829,
    0x2F342D28,0x29343328,0x2532372A,0x232E392E,0x242B3831,0x24242424,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x35374240,0x3B373C40,0x40373640,0x4337333F,0x4339313B,0x433D3238,0x42413537,
    0x3D423B35,0x37414136,0x323F4538,0x313C473B,0x3239463F,0x2F2F2F2F,0x1A1A1A1A,0x1A1A1A1A,
    0x49494949,0x3D414845,0x43404245,0x463F3D44,0x453D3B43,0x453E3B42,0x45423B3F,0x46473E3E,
    0x454A433E,0x3E48463D,0x3943483E,0x38404A42,0x39404B44,0x3E3E3E3E,0x1B1B1B1B,0x1B1B1B1B,
};

static void make_nes_pal_uv(uint8_t *u,uint8_t *v)
{
    for (int c = 0; c < 16; c++) {
        if (c == 0 || c > 12)
            ;
        else {
            float a = 2*M_PI*(c-1)/12 + 2*M_PI*(180-33)/360;    // just guessing at hue adjustment for PAL
            u[c] = cos(a)*127;
            v[c] = sin(a)*127;

            // get the phase offsets for even and odd pal lines
            //_p0[c] = atan2(0.436798*u[c],0.614777*v[c])*256/(2*M_PI) + 192;
            //_p1[c] = atan2(0.436798*u[c],-0.614777*v[c])*256/(2*M_PI) + 192;
        }
    }
}

// TODO. scale the u,v to fill range
uint32_t yuv_palette(int r, int g, int b)
{
    float y = 0.299 * r + 0.587 *g + 0.114 * b;
    float u = -0.147407 * r - 0.289391 * g + 0.436798 * b;
    float v =  0.614777 * r - 0.514799 * g - 0.099978 * b;
    int luma = y/255*(WHITE_LEVEL-BLACK_LEVEL) + BLACK_LEVEL;
    uint8_t ui = u;
    uint8_t vi = v;
    return ((luma & 0xFF00) << 16) | ((ui & 0xF8) << 8) | (vi >> 3); // luma:0:u:v
}

void make_yuv_palette(const char* name, const uint32_t* pal, int len);

extern rgb_t nes_palette[64];
extern "C" void pal_generate();
void make_alt_pal()
{
    pal_generate();
    uint32_t pal[64];
    for (int i = 0; i < 64; i++) {
        auto p = nes_palette[i];
        pal[i] = (p.r << 16) | (p.g << 8) | p.b;
    }
    make_yuv_palette("_nes_yuv",pal,64);
}

static const float _nes_luma[] = {
    0.50,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.29,0.00,0.02,0.02,
    0.75,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.45,0.24,0.04,0.04,
    1.00,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.73,0.47,0.05,0.05,
    1.00,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.90,0.77,0.07,0.07,
};

static void make_nes_palette(int phases)
{
    float saturation = 0.5;
    printf("uint32_t nes_%d_phase[64] = {\n",phases);
    for (int i = 0; i < 64; i++) {
        int chroma = (i & 0xF);
        int luma = _nes_luma[i]*(WHITE_LEVEL-BLACK_LEVEL) + BLANKING_LEVEL;

        int p[8] = {0};
        for (int j = 0; j < phases; j++)
            p[j] = luma;  // 0x1D is really black, really BLANKING_LEVEL

        chroma -= 1;
        if (chroma >= 0 && chroma < 12) {
            for (int j = 0; j < phases; j++)
                p[j] += sin(2*M_PI*(5 + chroma + (12/phases)*j)/12) * BLANKING_LEVEL/2*saturation;   // not sure why 5 is the right offset
        }

        uint32_t pi = 0;
        for (int j = 0; j < 4; j++)
            pi = (pi << 8) | p[j] >> 8;
        printf("0x%08X,",pi);
        if ((i & 7) == 7)
            printf("\n");
    }
    printf("};\n");
}

uint8_t* _nofrendo_rom = 0;
extern "C"
char *osd_getromdata()
{
    return (char *)_nofrendo_rom;
}

extern "C"
int nes_emulate_init(const char* path, int width, int height);

extern "C"
uint8_t** nes_emulate_frame(bool draw_flag);

static void (*nes_sound_cb)(void *buffer, int length) = 0;

extern uint32_t nes_pal[256];

const char* _nes_help[] = {
    "Keyboard:",
    "  Arrow Keys - D-Pad",
    "  Left Shift - Button A",
    "  Option     - Button B",
    "  Return     - Start",
    "  Tab        - Select",
    "",
    "Wiimote (held sideways):",
    "  +          - Start",
    "  -          - Select",
    "  + & -      - Reset",
    "  A,1        - Button A",
    "  B,2        - Button B",
    0
};

const char* _nes_ext[] = {
    "nes",
    0
};

extern "C" int state_save_path(const char *fn);
extern "C" int state_load_path(const char *fn);

int _audio_frequency;
extern "C"
void osd_getsoundinfo(sndinfo_t *info)
{
    info->sample_rate = _audio_frequency;
    info->bps = 8;
}

extern "C"
void osd_setsound(void (*playfunc)(void *buffer, int length))
{
    nes_sound_cb = playfunc;
}

std::string to_string(int i);
class EmuNofrendo : public Emu {
    uint8_t** _lines;
public:
    EmuNofrendo(int ntsc) : Emu("nofrendo",256,240,ntsc,(16 | (1 << 8)),4,EMU_NES)    // audio is 16bit, 3 or 6 cc width
    {
        _lines = 0;
        _ext = _nes_ext;
        _help = _nes_help;
        _audio_frequency = audio_frequency;
    }

    virtual void gen_palettes()
    {
        make_nes_palette(3);
        make_nes_palette(4);
        make_alt_pal();
    }

    virtual int info(const string& file, vector<string>& strs)
    {
        string ext = get_ext(file);
        if (ext == "sns") {
            strs.push_back(file.substr(file.find_last_of("/") + 1));
            strs.push_back("Saved game");
            return 0;
        }
        uint8_t hdr[15];
        int len = Emu::head(file,hdr,sizeof(hdr));
        string name = file.substr(file.find_last_of("/") + 1);
        strs.push_back(name);
        strs.push_back(::to_string(len/1024) + "k NES Cartridge");
        strs.push_back("");
        if (hdr[0] == 'N' && hdr[1] == 'E' && hdr[2] == 'S') {
            int prg = hdr[4] * 16;
            int chr = hdr[5] * 8;
            int mapper = (hdr[6] >> 4) | (hdr[7] & 0xF0);
            char buf[64];
            sprintf(buf,"MAP:%d",mapper);
            strs.push_back(buf);
            sprintf(buf,"PRG:%dk",prg);
            strs.push_back(buf);
            sprintf(buf,"CHR:%dk",chr);
            strs.push_back(buf);
        }
        return 0;
    }

    void pad(int pressed, int index)
    {
        event_t e = event_get(index);
        e(pressed);
    }

    enum {
        event_joypad1_up_ = 1,
        event_joypad1_down_ = 2,
        event_joypad1_left_ = 4,
        event_joypad1_right_ = 8,
        event_joypad1_start_ = 16,
        event_joypad1_select_ = 32,
        event_joypad1_a_ = 64,
        event_joypad1_b_ = 128,

        event_soft_reset_ = 256,
        event_hard_reset_ = 512
    };

    const int _nes_1[11] = {
        event_joypad1_up,
        event_joypad1_down,
        event_joypad1_left,
        event_joypad1_right,
        event_joypad1_start,
        event_joypad1_select,
        event_joypad1_a,
        event_joypad1_b,

        event_soft_reset,
        event_hard_reset,
        0
    };

    const int _nes_2[9] = {
        event_joypad2_up,
        event_joypad2_down,
        event_joypad2_left,
        event_joypad2_right,
        event_joypad2_start,
        event_joypad2_select,
        event_joypad2_a,
        event_joypad2_b,
        0
    };

    // Rotated 90%
    const uint32_t _common_nes[16] = {
        0,  // msb
        0,
        0,
        event_joypad1_start_,       // PLUS
        event_joypad1_left_,        // UP
        event_joypad1_right_,       // DOWN
        event_joypad1_up_,          // RIGHT
        event_joypad1_down_,        // LEFT

        0, // HOME
        0,
        0,
        event_joypad1_select_,  // MINUS
        event_joypad1_a_,      // A
        event_joypad1_b_,      // B
        event_joypad1_b_,      // ONE
        event_joypad1_a_,      // TWO
    };

    const uint32_t _classic_nes[16] = {
        event_joypad1_right_,    // RIGHT
        event_joypad1_down_,     // DOWN
        0,                       // LEFT_TOP
        event_joypad1_select_,    // MINUS
        0,                        // HOME
        event_joypad1_start_,     // PLUS
        0,                    // RIGHT_TOP
        0,

        0,                  // LOWER_LEFT
        event_joypad1_b_,   // B
        0,                  // Y
        event_joypad1_a_,   // A
        0,                  // X
        0,                  // LOWER_RIGHT
        event_joypad1_left_, // LEFT
        event_joypad1_up_   // UP
    };

    const uint32_t _generic_nes[16] = {
        0,                  // GENERIC_OTHER   0x8000
        0,                  // GENERIC_FIRE_X  0x4000  // RETCON
        0,                  // GENERIC_FIRE_Y  0x2000
        0,                  // GENERIC_FIRE_Z  0x1000

        event_joypad1_a_,      //GENERIC_FIRE_A  0x0800
        event_joypad1_b_,      //GENERIC_FIRE_B  0x0400
        0,                      //GENERIC_FIRE_C  0x0200
        0,                      //GENERIC_RESET   0x0100     // ATARI FLASHBACK

        event_joypad1_start_,   //GENERIC_START   0x0080
        event_joypad1_select_,  //GENERIC_SELECT  0x0040
        event_joypad1_a_,      //GENERIC_FIRE    0x0020
        event_joypad1_right_,  //GENERIC_RIGHT   0x0010

        event_joypad1_left_,   //GENERIC_LEFT    0x0008
        event_joypad1_down_,   //GENERIC_DOWN    0x0004
        event_joypad1_up_,      //GENERIC_UP      0x0002
        0,                      //GENERIC_MENU    0x0001
    };

    // raw HID data. handle WII/IR mappings
    virtual void hid(const uint8_t* d, int len)
    {
        if (!_nofrendo_rom)
            return;                             // no game loaded: nothing to send the buttons to
        if (d[0] != 0x32 && d[0] != 0x42)
            return;
        bool ir = *d++ == 0x42;

        for (int i = 0; i < 2; i++) {
            uint32_t p;
            if (ir) {
                int m = d[0] + (d[1] << 8);
                if (i == 0) {                       // player 1: save request (both select buttons)
                    if (m & GENERIC_FIRE_Y)
                        m = 0;                      // open-saves-page request: not a game button
                    bool down = (m & GENERIC_FIRE_X) != 0;
                    if (down && !_save_down)
                        _save_req = true;
                    _save_down = down;
                    if (down)
                        m = 0;                      // keep these buttons away from the game
                }
                p = generic_map(m,_generic_nes);
                d += 2;
            } else
                p = wii_map(i,_common_nes,_classic_nes);

            // reset on select + start held at the same time
            if ((p & event_joypad1_select_) && (p & event_joypad1_start_))
                pad(1,event_soft_reset);

            const int* m = i ? _nes_2 : _nes_1;
            for (int e = 0; m[e]; e++)
            {
                pad((p & 1),m[e]);
                p >>= 1;
            }
        }
    }

    /*
     Return - Joypad 1 Start
     Tab - Joypad 1 Select
     */
    virtual void key(int keycode, int pressed, int mods)
    {
        switch (keycode) {
            case 82: pad(pressed,event_joypad1_up); break;
            case 81: pad(pressed,event_joypad1_down); break;
            case 80: pad(pressed,event_joypad1_left); break;
            case 79: pad(pressed,event_joypad1_right); break;

            case 21: pad(pressed,event_soft_reset); break; // soft reset - 'r'
            case 23: pad(pressed,event_hard_reset); break; // hard reset - 't'

            case 61: pad(pressed,event_joypad1_start); break; // F4
            case 62: pad(pressed,((KEY_MOD_LSHIFT|KEY_MOD_RSHIFT) & mods) ? event_hard_reset : event_soft_reset); break; // F5

            case 40: pad(pressed,event_joypad1_start); break; // return
            case 43: pad(pressed,event_joypad1_select); break; // tab

            case 225: pad(pressed,event_joypad1_a); break; // left shift key event_joypad1_a
            case 226: pad(pressed,event_joypad1_b); break; // option key event_joypad1_b

            //case 59: system(pressed,INPUT_PAUSE); break; // F2
            //case 61: system(pressed,INPUT_START); break; // F4
            //case 62: system(pressed,((KEY_MOD_LSHIFT|KEY_MOD_RSHIFT) & mods) ? INPUT_HARD_RESET : INPUT_SOFT_RESET); break; // F5
        }
    }

    // ---- saved games ----
    std::string _rom_path;
    bool _save_req = false, _save_down = false;
    const char* _msg = 0;
    char _msgbuf[32];

    // save the running game to "<folder>/SAVE nn - <game>.sns" using the first free number
    void do_save()
    {
        if (_rom_path.empty())
            return;
        size_t slash = _rom_path.find_last_of('/');
        string dir = _rom_path.substr(0, slash + 1);
        string file = _rom_path.substr(slash + 1);
        size_t dot = file.find_last_of('.');
        string base = dot == string::npos ? file : file.substr(0, dot);

        // number the save one higher than the highest existing SAVE number (bigger = newer)
        bool used[100] = {false};
        int n = 0;
        DIR* d = opendir(dir.empty() ? "/" : dir.substr(0, dir.size() - 1).c_str());
        if (d) {
            struct dirent* e;
            while ((e = readdir(d)) != NULL) {
                string nm = e->d_name;
                if (nm.size() >= 7 && nm.compare(0, 5, "SAVE ") == 0 && get_ext(nm) == "sns") {
                    int k = atoi(nm.c_str() + 5);
                    if (k >= 1 && k <= 99) {
                        used[k] = true;
                        if (k > n)
                            n = k;
                    }
                }
            }
            closedir(d);
        }
        n++;
        if (n > 99) {                               // numbers ran out: reuse the first free one
            for (n = 1; n <= 99 && used[n]; n++)
                ;
            if (n > 99)
                n = 99;
        }
        char num[8];
        sprintf(num,"%02d",n);
        string path = dir + "SAVE " + num + " - " + base + ".sns";

        if (state_save_path(path.c_str()) == 0)
            sprintf(_msgbuf,"GAME SAVED %02d",n);
        else
            sprintf(_msgbuf,"SAVE FAILED");
        _msg = _msgbuf;
    }

    // open "SAVE nn - <game>.sns": start the game, then restore the saved state
    int insert_save(const std::string& path)
    {
        size_t slash = path.find_last_of('/');
        string dir = path.substr(0, slash + 1);
        string name = path.substr(slash + 1);
        size_t dot = name.find_last_of('.');
        if (dot != string::npos)
            name = name.substr(0, dot);
        size_t dash = name.find(" - ");
        if (name.compare(0,5,"SAVE ") != 0 || dash == string::npos) {
            printf("nofrendo: bad save name %s\n",path.c_str());
            return -1;
        }
        string base = name.substr(dash + 3);
        static const char* exts[] = { ".nes", ".NES", ".Nes", 0 };
        string rom;
        for (int i = 0; exts[i] && rom.empty(); i++) {
            string c = dir + base + exts[i];
            FILE* f = fopen(c.c_str(),"rb");
            if (f) {
                fclose(f);
                rom = c;
            }
        }
        if (rom.empty()) {
            printf("nofrendo: game for save %s not found\n",path.c_str());
            return -1;
        }
        int r = insert(rom,1,0);
        if (r == 0) {
            if (state_load_path(path.c_str()) == 0)
                _msg = "GAME LOADED";
            else
                _msg = "LOAD FAILED";
        }
        return r;
    }

    virtual int insert(const std::string& path, int flags, int disk_index)
    {
        if (get_ext(path) == "sns")
            return insert_save(path);
        unmap_file(_nofrendo_rom);
        _nofrendo_rom = 0;
        g_nes_skip = 0; _t_draw = 0; _t_nodraw = 0; _want = 0; _want_count = 0;   // measure the new game afresh
        printf("nofrendo inserting %s\n",path.c_str());

        uint8_t h[16];
        int len = head(path,h,sizeof(h));
        if (len < 0) {
            printf("nofrendo can't open %s\n",path.c_str());
            return -1;
        }

        printf("nofrendo %s is %d bytes\n",path.c_str(),len);
        _nofrendo_rom = map_file(path.c_str(),len);
        if (!_nofrendo_rom) {
            printf("nofrendo can't map %s\n",path.c_str());
            return -1;
        }

        nes_emulate_init(path.c_str(),width,height);
        _lines = nes_emulate_frame(true);   // first frame!
        _rom_path = path;
        return 0;
    }

    // smoothed time (microseconds) of a frame with and without drawing
    int _t_draw = 0, _t_nodraw = 0;
    int _want = 0, _want_count = 0;

    static void smooth(int& avg, int sample)
    {
        avg = avg ? avg + (sample - avg) / 8 : sample;
    }

    // pick the number of undrawn frames per refresh that keeps the game closest to full speed
    int choose_skip()
    {
        const int F = standard ? 16639 : 20000;     // one screen refresh in us
        const int OVERHEAD = 2500;                  // audio, menu, scheduling
        int best = 0, best_speed = -1;
        for (int s = 0; s < 4; s++) {
            int nd = _t_nodraw ? _t_nodraw : (_t_draw * 6) / 10;   // guess until an undrawn frame was timed
            int t = _t_draw + s * nd + OVERHEAD;
            int periods = (t + F - 1) / F;          // refreshes this update will occupy
            if (periods < 1) periods = 1;
            if (1 + s > periods)
                continue;                           // would run faster than normal
            int speed = (1000 * (1 + s)) / periods;
            if (speed > best_speed) {
                best_speed = speed;
                best = s;
            }
        }
        return best;
    }

    bool _flip = false;

    void draw_frame()
    {
        int64_t b = esp_timer_get_time();
        _lines = nes_emulate_frame(true);
        smooth(_t_draw, (int)(esp_timer_get_time() - b));
    }

    virtual int update()
    {
        if (!_nofrendo_rom)
            return 0;

        if (_save_req) {                             // both select buttons pressed: save the game now
            _save_req = false;
            do_save();
        }
        if (_msg) {
            gui_msg(_msg);
            _msg = 0;
        }

        const int skips = g_nes_skip;
        g_nes_frames = 1 + skips;
        // With frame skipping only some game frames are drawn. Many games hide different sprites on
        // odd and even frames (that is their flicker), so always drawing the same kind of frame would
        // make some characters vanish for good. So the drawn frame alternates between last and first.
        _flip = !_flip;
        const bool draw_first = skips > 0 && _flip;
        if (draw_first)
            draw_frame();
        for (int i = 0; i < skips; i++) {
            int64_t a = esp_timer_get_time();
            nes_emulate_frame(false);                // run the game, skip drawing
            smooth(_t_nodraw, (int)(esp_timer_get_time() - a));
        }
        if (!draw_first)
            draw_frame();

        // change the skip only after the same answer 20 times in a row (avoids flicker between modes)
        int w = choose_skip();
        if (w == g_nes_skip) {
            _want_count = 0;
        } else if (w == _want) {
            if (++_want_count >= 20) {
                g_nes_skip = w;
                _want_count = 0;
            }
        } else {
            _want = w;
            _want_count = 1;
        }
        return 0;
    }

    virtual uint8_t** video_buffer()
    {
        return _lines;
    }
    
    virtual int audio_buffer(int16_t* b, int len)
    {
        int n = 0;
        for (int i = 0; i <= g_nes_skip; i++)        // sound for every emulated frame
            n += frame_sample_count();
        if (n > len / 2)
            n = len / 2;
        if (nes_sound_cb) {
            nes_sound_cb(b,n);  // 8 bit unsigned
            uint8_t* b8 = (uint8_t*)b;
            for (int i = n-1; i >= 0; i--)
                b[i] = (b8[i] ^ 0x80) << 8;  // turn it back into signed 16
        }
        else
            memset(b,0,2*n);
        return n;
    }

    virtual const uint32_t* ntsc_palette() { return cc_width == 3 ? nes_3_phase : nes_4_phase; };
    // PAL colour strength: 1.0 = original, lower = weaker colour (less crease / diagonal pattern, duller picture)
    #define PAL_CHROMA 0.70f
    static const uint32_t* pal_soft()
    {
        static uint32_t* t = 0;
        if (!t) {
            const int n = sizeof(_nes_yuv_4_phase_pal) / sizeof(uint32_t);
            t = new uint32_t[n];
            for (int i = 0; i < n; i++) {
                uint32_t v = _nes_yuv_4_phase_pal[i], out = 0;
                int b[4], m = 0;
                for (int k = 0; k < 4; k++) {
                    b[k] = (v >> (8 * k)) & 0xFF;
                    m += b[k];
                }
                m /= 4;                                         // average = brightness, kept as it was
                for (int k = 0; k < 4; k++) {
                    int x = m + (int)((b[k] - m) * PAL_CHROMA); // only the colour swing is reduced
                    if (x < 0) x = 0;
                    if (x > 255) x = 255;
                    out |= (uint32_t)x << (8 * k);
                }
                t[i] = out;
            }
        }
        return t;
    }
    virtual const uint32_t* pal_palette() { return PAL_CHROMA >= 0.999f ? _nes_yuv_4_phase_pal : pal_soft(); };
    virtual const uint32_t* rgb_palette() { return nes_pal; };

    virtual int make_default_media(const string& path)
    {
        unpack((path + "/sokoban.nes").c_str(),sokoban_nes,sizeof(sokoban_nes));
        unpack((path + "/chase.nes").c_str(),chase_nes,sizeof(chase_nes));
        unpack((path + "/tokumaru_raycast.nes").c_str(),tokumaru_raycast_nes,sizeof(tokumaru_raycast_nes));
        return 0;
    }
};

Emu* NewNofrendo(int ntsc)
{
    return new EmuNofrendo(ntsc);
}



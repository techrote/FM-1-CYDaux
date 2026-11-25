#include "fm1/profile.hpp"
#include <cstring>

namespace fm1 {
namespace {
// Initial values seed Demo only. A connected instrument starts with unknown values.
// These maps describe the pinned sources in docs/upstream-sources.json.
const Profile kProfiles[] = {
    {
        "melodee", "Melodee", "Four tracks / BLE MIDI", "1.6.1",
        4, {{1, 2, 3, 4, 0, 0, 0, 0}},
        {{"Track 1", "Track 2", "Track 3", "Track 4", "", "", "", ""}},
        {{{"Tone", 74, 96}, {"Reso", 71, 28}, {"Attack", 73, 8}, {"Release", 72, 48},
          {"Level", 7, 100}, {"Pan", 10, 64}, {"Decay", 75, 64}, {"Glide", 5, 0}}},
        true, false, false, false,
        "Set CH1-4 routing; BLE radio on and VISIBLE. Use FM-1 PLAY."
    },
    {
        "sloop", "SLOOP", "Three synths + drums", "2.8.5",
        4, {{1, 2, 3, 10, 0, 0, 0, 0}},
        {{"Synth 1", "Synth 2", "Synth 3", "Drums", "", "", "", ""}},
        {{{"Tone", 74, 64}, {"Reso", 71, 28}, {"Attack", 73, 8}, {"Release", 72, 48},
          {"Level", 7, 100}, {"Pan", 10, 64}, {"Reverb", 91, 24}, {"Delay", 94, 16}}},
        false, false, true, true,
        "USB bridge; match channels 1/2/3/10. LIVE needs SLOOP protocol 12+."
    },
    {
        "skiff", "Skiff", "Six configurable tracks", "2.7.1",
        6, {{1, 2, 3, 4, 5, 6, 0, 0}},
        {{"Track 1", "Track 2", "Track 3", "Track 4", "Track 5", "Track 6", "", ""}},
        {{{"Tone", 74, 96}, {"Reso", 71, 28}, {"Attack", 73, 8}, {"Release", 72, 48},
          {"Level", 7, 100}, {"Pan", 10, 64}, {"Reverb", 91, 24}, {"Delay", 94, 16}}},
        false, false, false, true,
        "USB bridge; match track channels 1-6. Use FM-1 PLAY."
    },
    {
        "x0x", "X0X", "303 A / B performance", "6515e77",
        2, {{2, 3, 0, 0, 0, 0, 0, 0}},
        {{"303 A", "303 B", "", "", "", "", "", ""}},
        {{{"Cutoff", 16, 84}, {"Reso", 17, 64}, {"Env amt", 18, 72}, {"Decay", 19, 64},
          {"Accent", 20, 64}, {"Wave", 21, 0}, {"Drive", 22, 32}, {"Slide", 23, 48}}},
        false, true, false, false,
        "USB bridge; 303 A/B on channels 2/3. CC OUT KNOBS/ALL enables feedback."
    }
};
} // namespace

std::size_t profileCount() {
    return sizeof(kProfiles) / sizeof(kProfiles[0]);
}
const Profile& profileAt(std::size_t index) {
    return kProfiles[index < profileCount() ? index : 0];
}
int profileIndex(const char* id) {
    if (!id) return -1;
    for (std::size_t i = 0; i < profileCount(); ++i)
        if (std::strcmp(id, kProfiles[i].id) == 0) return static_cast<int>(i);
    return -1;
}
} // namespace fm1

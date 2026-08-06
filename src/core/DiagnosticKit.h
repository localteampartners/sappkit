#pragma once
// Generated in-memory diagnostic drum kit. Lets SappKit make sound (and run
// choke/round-robin/pad-mapping tests) without redistributing sample content.
//
// Layout: 16 GM-mapped one-shot sounds (kick 36 … open conga 63), including:
//   * 2 velocity layers + 2 round robins on kick and snare
//   * a hi-hat choke family (42 closed / 44 pedal / 46 open, group 1,
//     off_by 1, off_mode=time)
//   * lorand/hirand alternate takes on the tambourine (54)
//   * pitch/amp humanize opcodes on toms, hats, and congas
//   * note_polyphony=2 on the kick

#include <sapp/sounds/InstrumentDefinition.h>

namespace sapp::kit {

struct DiagnosticKitOptions {
    uint32_t sampleRate = 48000;
    uint32_t seed = 20260806;
};

sapp::sounds::InstrumentPtr makeDiagnosticKit(const DiagnosticKitOptions& options = {});

} // namespace sapp::kit

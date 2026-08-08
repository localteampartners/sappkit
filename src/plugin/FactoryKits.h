#pragma once
// SappKit factory kit programs: named kits selectable from the host program
// API and via MIDI program change (SappLink set_patches). Program 0 is the
// built-in diagnostic kit; the rest resolve against installed GET SOUNDS
// libraries under the shared samples root (library key + filename pattern,
// so extraction layout differences don't matter). A program whose library
// is not installed is a no-op with a status message.
//
// Programs are ADDITIVE: they reuse the exact same kit-load path as the
// sounds browser (loadKitSfz + per-kit saved mixes), and never change
// parameter IDs, CC mappings, or state save/load.

#include <vector>

#include <juce_core/juce_core.h>

namespace sappkit::factorykits {

struct FactoryKit {
    const char* name;
    const char* libraryKey;    // folder under the samples root ("" = built-in)
    const char* mustContain;   // filename substring picking the main .sfz
};

// Fixed program table; program N is all()[N]. Order is a compatibility
// contract with the SappLink manifest — append, never reorder.
const std::vector<FactoryKit>& all();

// Resolve program N's .sfz against the samples root (message thread — walks
// the filesystem). Invalid File for program 0 (built-in diagnostic kit) or
// when the library is not installed.
juce::File resolveKit(int index, const juce::File& samplesRoot);

// Reverse lookup: which program corresponds to this loaded kit path?
// Empty path = 0 (diagnostic). -1 when the kit is not in the table.
int programForKitFile(const juce::String& path, const juce::File& samplesRoot);

} // namespace sappkit::factorykits

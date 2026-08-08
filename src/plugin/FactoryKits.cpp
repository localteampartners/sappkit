#include "FactoryKits.h"

namespace sappkit::factorykits {

const std::vector<FactoryKit>& all()
{
    // Library keys match the GET SOUNDS registry (SoundsPanel.cpp).
    // Patterns pick the main playable kit inside each library; trailing
    // dots pin exact filenames where siblings share a prefix.
    static const std::vector<FactoryKit> table {
        { "Diagnostic Kit",       "",                 ""                     },
        { "Black Pearl 4pc",      "avl-drumkits",     "Black_Pearl_4pc."     },
        { "Black Pearl 5pc",      "avl-drumkits",     "Black_Pearl_5pc."     },
        { "Red Zeppelin 4pc",     "avl-drumkits",     "Red_Zeppelin_4pc."    },
        { "Red Zeppelin 5pc",     "avl-drumkits",     "Red_Zeppelin_5pc."    },
        { "Muldjord Kit",         "muldjord-kit",     "DrumGizmo MuldjordKit" },
        { "Virtuosity Basic Kit", "virtuosity-drums", "01-basic-kit"         },
        { "Virtuosity Full Kit",  "virtuosity-drums", "02-full-kit"          },
        { "Gogodze Lo-Fi Kit",    "gogodze-phu",      "Kit."                 },
        { "Gogodze Lo-Fi Kit B",  "gogodze-phu",      "Kit_b"                },
        { "Latin Percussion",     "latin-percussion", "Latin-Percussion"     },
    };
    return table;
}

juce::File resolveKit(int index, const juce::File& samplesRoot)
{
    const auto& table = all();
    if (index <= 0 || index >= int(table.size()))
        return {};
    const auto& kit = table[size_t(index)];

    const auto dir = samplesRoot.getChildFile(kit.libraryKey);
    if (!dir.isDirectory())
        return {};
    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.sfz");
    files.sort();   // deterministic pick when several files match
    for (const auto& file : files) {
        const auto path = file.getFullPathName().replaceCharacter('\\', '/');
        if (path.contains("/includes/") || path.contains("/modules/") ||
            path.contains("/Data/") || path.contains("/mappings/"))
            continue;
        if (file.getFileName().contains(kit.mustContain))
            return file;
    }
    return {};
}

int programForKitFile(const juce::String& path, const juce::File& samplesRoot)
{
    if (path.isEmpty())
        return 0;   // diagnostic kit
    for (int i = 1; i < int(all().size()); ++i)
        if (resolveKit(i, samplesRoot).getFullPathName() == path)
            return i;
    return -1;
}

} // namespace sappkit::factorykits

#include <Menu/CallbackSlots.h>
#include <Search/SavePath.h>

#include <cstdio>
#include <cstdlib>
#include <source_location>

namespace
{
    int First(int value) { return value + 1; }
    int Second(int value) { return value + 2; }
    int ProbeOne(int value) { return value + 3; }
    int ProbeTwo(int value) { return value + 4; }

    void Expect(bool passed, std::source_location location = std::source_location::current())
    {
        if (!passed) {
            std::fprintf(stderr, "Check failed at %s:%u\n", location.file_name(), location.line());
            std::exit(EXIT_FAILURE);
        }
    }
}

int main()
{
    using Callback = int (*)(int);
    SaveLoadFix::CallbackSlots<Callback, 2> slots;
    const std::array<Callback, 2> probes{ ProbeOne, ProbeTwo };

    Expect(slots.Select("PlaySound", First, probes) == First);
    Expect(slots.Select("LoadGame", nullptr, probes) == nullptr);
    Expect(slots.Select("LoadGame", First, probes) == ProbeOne);
    Expect(slots.Select("LoadGame", First, probes) == ProbeOne);
    Expect(slots.Select("LoadGame", Second, probes) == ProbeTwo);
    Expect(slots.Select("SaveGame", First, probes) == First);

    const auto [firstName, first] = slots.Get(0);
    const auto [secondName, second] = slots.Get(1);
    Expect(firstName == "LoadGame" && first(10) == 11);
    Expect(secondName == "LoadGame" && second(10) == 12);
    Expect(SaveLoadFix::IsSaveCallback("GetCharacterList"));
    Expect(SaveLoadFix::IsSaveCallback("PrepSaveGameScreenshot"));
    Expect(SaveLoadFix::IsSaveCallback("SAVE"));
    Expect(SaveLoadFix::IsSaveCallback("LOAD"));

    Expect(SaveLoadFix::IsSaveSearch(R"(C:\Game\__MO_Saves\*)"));
    Expect(SaveLoadFix::IsSaveSearch("C:/Game/SAVES/*.skse"));
    Expect(SaveLoadFix::IsSaveSearch("*.ESS"));
    Expect(!SaveLoadFix::IsSaveSearch(R"(C:\Game\SavesBackup\*)"));
    Expect(!SaveLoadFix::IsSaveSearch("Data/*.esp"));
}

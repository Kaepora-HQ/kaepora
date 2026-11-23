// The version, seed and settings of a seed's spoiler log, read as the window reads the log, give the id the seed's
// plan carries. The seed made again gets the same id, and another seed with the same settings, another setting or
// another version gets another.
#include "generator/generate.h"

#include <cstdint>
#include <cstdio>
#include <string>

static uint32_t Planned(const std::string& seed, const SeedSettings& settings) {
    return Generate(seed, settings).plan.at("seed_id").get<uint32_t>();
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        if (!ok) {
            std::printf("failed: %s\n", what);
            ++failures;
        }
    };
    const GeneratedSeed made = Generate("OWL", {});
    const uint32_t id = made.plan.at("seed_id").get<uint32_t>();
    const nlohmann::json log = nlohmann::json::parse(made.spoilerLog);
    const auto logged = [&](const std::string& version) {
        return SeedId(version, log.at("seed").get<std::string>(), log.at("settings"));
    };
    check(logged(log.at("kaepora").get<std::string>()) == id, "the spoiler log gives the plan's id");
    check(logged("0.0.0") != id, "another version gives another id");
    check(Planned("OWL", {}) == id, "the seed made again gets the same id");
    check(Planned("HOOT", {}) != id, "another seed with the same settings gets another id");
    check(Planned("OWL", { .lens = Lens::NeverNeeded }) != id, "another setting gives another id");
    std::printf("%s\n", failures == 0 ? "all passed" : "some failed");
    return failures == 0 ? 0 : 1;
}

#pragma once

#include <cstdint>

#include "core/library/database.hpp"

namespace mp::core::library {

struct WorkStats {
    std::uint64_t tracks = 0;        // tracks looked at
    std::uint64_t tracksInWorks = 0; // of those: assigned to a work
    std::uint64_t worksCreated = 0;
};

// Third stage of a scan (after resolveEntities): groups tracks into `works` (SPEC §7b, D21, D24).
// Looks at every track with works_done = 0, plus the other tracks of their albums (a new track can change
// how its neighbours group). Reads the effective tags from v_tags (overrides applied, detector output not),
// writes tracks.work_id and the derived movement number/name (det_movement_*), and removes works nothing
// refers to anymore. One transaction on the caller's connection. Needs resolveEntities to have run (album ids,
// composer ids).
WorkStats resolveWorks(Database& db);

}  // namespace mp::core::library

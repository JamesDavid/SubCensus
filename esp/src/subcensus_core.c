/* subcensus_core.c — unity build of shared/core into the ESP firmware (System §2).
 *
 * Same rationale as zero/subcensus_core.c: compile the shared C logic core as a single
 * translation unit here rather than juggling external source paths in the build system.
 * Quoted #include resolves each core .c relative to its real location (shared/core/), and
 * their internal sibling-header includes resolve there too — so shared/core stays the single
 * source of truth (no copy, no drift). The ESP shares the Zero's CC1101 capture model, so it
 * gets the SAME feature vector / cadence / k-NN / CRC / differential logic (System §7, §7a).
 *
 * Unity-build constraint: file-scope `static` names across core .c files must be unique
 * (shared helpers live in shared/core/sc_util.h). The core is float-only (single-precision),
 * which suits the ESP32 FPU too.
 */

#include "../../shared/core/sc_crc.c"
#include "../../shared/core/sc_sub.c"
#include "../../shared/core/sc_pulse.c"
#include "../../shared/core/sc_feature.c"
#include "../../shared/core/sc_cadence.c"
#include "../../shared/core/sc_knn.c"
#include "../../shared/core/sc_occupancy.c"
#include "../../shared/core/sc_diff.c"
#include "../../shared/core/sc_fieldmap.c"
#include "../../shared/core/sc_slice.c"

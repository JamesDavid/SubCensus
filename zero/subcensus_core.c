/* subcensus_core.c — unity build of shared/core into the FAP (System §2).
 *
 * fbt globs FAP sources within the app tree and its variant-dir build can't reference `../`
 * source paths, so instead of listing the core sources in application.fam we compile the
 * shared C logic core as a single translation unit here. Quoted #include resolves each
 * core .c relative to its real location (shared/core/), and their internal `#include`
 * of sibling headers resolve there too — so shared/core stays the single source of truth
 * (no copy, no drift) and `ufbt` builds standalone.
 *
 * Unity-build constraint: file-scope `static` names across core .c files must be unique
 * (see shared/core/sc_util.h for the shared helpers hoisted to satisfy this).
 */

#include "../shared/core/sc_crc.c"
#include "../shared/core/sc_sub.c"
#include "../shared/core/sc_pulse.c"
#include "../shared/core/sc_feature.c"
#include "../shared/core/sc_cadence.c"
#include "../shared/core/sc_knn.c"
#include "../shared/core/sc_occupancy.c"
#include "../shared/core/sc_diff.c"
#include "../shared/core/sc_fieldmap.c"
#include "../shared/core/sc_slice.c"

#ifndef SPACES_H
#define SPACES_H
#include "src/globals.h"
namespace v8 {
namespace internal {
class MemoryChunk {
public:
  using Flags = uintptr_t;
  static const intptr_t kSizeOffset = 0;
  static const intptr_t kFlagsOffset = kSizeOffset + kSizetSize;
  static const intptr_t kAreaStartOffset = kFlagsOffset + kIntptrSize;
  static const intptr_t kAreaEndOffset = kAreaStartOffset + kPointerSize;
  static const intptr_t kReservationOffset = kAreaEndOffset + kPointerSize;
  static const intptr_t kOwnerOffset = kReservationOffset + 2 * kPointerSize;

  enum Flag {
    NO_FLAGS = 0u,
    IS_EXECUTABLE = 1u << 0,
    POINTERS_TO_HERE_ARE_INTERESTING = 1u << 1,
    POINTERS_FROM_HERE_ARE_INTERESTING = 1u << 2,
    // A page in new space has one of the next to flags set.
    IN_FROM_SPACE = 1u << 3,
    IN_TO_SPACE = 1u << 4,
    NEW_SPACE_BELOW_AGE_MARK = 1u << 5,
    EVACUATION_CANDIDATE = 1u << 6,
    NEVER_EVACUATE = 1u << 7,

    // Large objects can have a progress bar in their page header. These object
    // are scanned in increments and will be kept black while being scanned.
    // Even if the mutator writes to them they will be kept black and a white
    // to grey transition is performed in the value.
    HAS_PROGRESS_BAR = 1u << 8,

    // |PAGE_NEW_OLD_PROMOTION|: A page tagged with this flag has been promoted
    // from new to old space during evacuation.
    PAGE_NEW_OLD_PROMOTION = 1u << 9,

    // |PAGE_NEW_NEW_PROMOTION|: A page tagged with this flag has been moved
    // within the new space during evacuation.
    PAGE_NEW_NEW_PROMOTION = 1u << 10,

    // This flag is intended to be used for testing. Works only when both
    // FLAG_stress_compaction and FLAG_manual_evacuation_candidates_selection
    // are set. It forces the page to become an evacuation candidate at next
    // candidates selection cycle.
    FORCE_EVACUATION_CANDIDATE_FOR_TESTING = 1u << 11,

    // This flag is intended to be used for testing.
    NEVER_ALLOCATE_ON_PAGE = 1u << 12,

    // The memory chunk is already logically freed, however the actual freeing
    // still has to be performed.
    PRE_FREED = 1u << 13,

    // |POOLED|: When actually freeing this chunk, only uncommit and do not
    // give up the reservation as we still reuse the chunk at some point.
    POOLED = 1u << 14,

    // |COMPACTION_WAS_ABORTED|: Indicates that the compaction in this page
    //   has been aborted and needs special handling by the sweeper.
    COMPACTION_WAS_ABORTED = 1u << 15,

    // |COMPACTION_WAS_ABORTED_FOR_TESTING|: During stress testing evacuation
    // on pages is sometimes aborted. The flag is used to avoid repeatedly
    // triggering on the same page.
    COMPACTION_WAS_ABORTED_FOR_TESTING = 1u << 16,

    // |ANCHOR|: Flag is set if page is an anchor.
    ANCHOR = 1u << 17,

    // |SWEEP_TO_ITERATE|: The page requires sweeping using external markbits
    // to iterate the page.
    SWEEP_TO_ITERATE = 1u << 18
  };

  static const Flags kPointersToHereAreInterestingMask =
      POINTERS_TO_HERE_ARE_INTERESTING;

  static const Flags kPointersFromHereAreInterestingMask =
      POINTERS_FROM_HERE_ARE_INTERESTING;

};
}
}

#endif  // SPACES_H

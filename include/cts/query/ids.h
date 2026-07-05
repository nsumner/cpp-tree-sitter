#ifndef CTS_QUERY_IDS_H
#define CTS_QUERY_IDS_H

// The query subsystem's strong id types.
//
// Separate from query.h so predicates.h can use them without depending on
// Query, and separate from common.h because they are query vocabulary rather
// than library-wide vocabulary.

#include <cstdint>

#include <tree_sitter/api.h>


namespace ts {

enum class CaptureId        : uint32_t {};
enum class PredicateTokenId : uint32_t {};
enum class PatternIndex     : uint16_t {};


// How many times one capture may appear in one match of one pattern.
// Anything other than One or ZeroOrOne means QueryMatch::getNodeFor is giving
// the first of several.
enum class Quantifier : uint8_t {
  Zero       = TSQuantifierZero,
  ZeroOrOne  = TSQuantifierZeroOrOne,
  ZeroOrMore = TSQuantifierZeroOrMore,
  One        = TSQuantifierOne,
  OneOrMore  = TSQuantifierOneOrMore,
};

}

#endif

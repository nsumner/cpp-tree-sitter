#ifndef CTS_QUERY_H
#define CTS_QUERY_H

// The query subsystem is deliberately not included by <cpp-tree-sitter.h> to
// stay opt-in. Formatters for these types live in <cts/query/format.h>,
// which is a separate opt-in for the same reason <cts/format.h> is.

#include <cts/query/ids.h>
#include <cts/query/predicates.h>
#include <cts/query/query.h>

#endif

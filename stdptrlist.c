/*
 * std::list primitives - doubly-linked node allocation, splice, and erase.
 *
 * Backs every StdPtrList instantiation used by the server (file lists,
 * serial lists, tag lists). Nodes are allocated individually from the CRT
 * heap and threaded through prev/next pointers around a shared sentinel,
 * matching the MSVC std::list layout the binary was compiled against.
 */

#include "stl.h"

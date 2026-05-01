/*
 * std::list forwarding shim.
 *
 * The real list behaviour lives in stdptrlist.c; this file exists
 * purely so a matching object file is produced for stdlist.h
 * consumers.
 */

typedef int stdlist_shim_unit;

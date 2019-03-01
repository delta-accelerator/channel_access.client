#ifndef INCLUDE_GUARD_B54E04CF_6AE0_42D7_9474_8463C3E4A4F8
#define INCLUDE_GUARD_B54E04CF_6AE0_42D7_9474_8463C3E4A4F8

#include <Python.h>

namespace cac {

/** Create the pv type.
 * Returns new reference.
 */
PyObject* create_pv_type();

/** Destroy the pv type.
 */
void destroy_pv_type();

}

#endif

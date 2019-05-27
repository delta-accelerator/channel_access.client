#ifndef INCLUDE_GUARD_5008BBFD_8149_4E6A_84D6_230B7A01CC90
#define INCLUDE_GUARD_5008BBFD_8149_4E6A_84D6_230B7A01CC90

#include <vector>

#include <Python.h>

namespace cac {

/** Convert a python value to a channel access buffer.
 * dbr_type is the desired type of the buffer.
 *
 * If an error occurs the buffer is empty and an exception is set.
 */
std::pair<std::vector<uint8_t>, long> to_buffer(PyObject* value, short dbr_type, long count);

/** Convert a channel access buffer to a python value.
 * If an error occurs nullptr is returned and an exception is set.
 */
PyObject* from_buffer(void const* buffer, short dbr_type, long count);

}

#endif

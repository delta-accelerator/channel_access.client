#include "convert.hpp"
#include "cac.hpp"

#include <type_traits>
#include <Python.h>
#include <cadef.h>

namespace cac {
namespace {

//
// Put functions. Create an epics value from a python object.
//

bool put(PyObject* py_value, dbr_string_t* buffer)
{
    char* value;
    Py_ssize_t size;
    if (PyBytes_AsStringAndSize(py_value, &value, &size) != 0) return false;

    if (not value) {
        (*buffer)[0] = 0;
        return false;
    }
    if (size > MAX_STRING_SIZE-1) {
        PyErr_Format(PyExc_ValueError, "String length exceeds maximum epics string size of %i bytes", MAX_STRING_SIZE-1);
        return false;
    }

    memcpy(*buffer, value, size);
    (*buffer)[size] = 0;
    return true;
}

template <typename T>
auto put(PyObject* py_value, T* buffer) -> typename std::enable_if<std::is_integral<T>::value, bool>::type
{
    T value = PyLong_AsLong(py_value);
    if (PyErr_Occurred()) return false;

    *buffer = value;
    return true;
}

template <typename T>
auto put(PyObject* py_value, T* buffer) -> typename std::enable_if<std::is_floating_point<T>::value, bool>::type
{
    T value = PyFloat_AsDouble(py_value);
    if (PyErr_Occurred()) return false;

    *buffer = value;
    return true;
}

template<typename T>
bool put_value(std::vector<uint8_t>& buffer, PyObject* value, long& count)
{
    if (count == 1) {
        buffer.resize(sizeof(T));
        auto* buf = reinterpret_cast<T*>(buffer.data());

        return put(value, buf);
    }

    if (not PySequence_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "Expected sequence in put");
        return false;
    }

    Py_ssize_t size = PySequence_Size(value);
    if (PyErr_Occurred()) return false;

    if (count > size) {
        count = size;
    }

    buffer.resize(count * sizeof(T));
    auto* buf = reinterpret_cast<T*>(buffer.data());

    for (long i = 0; i < count; i++) {
        PyObject* val = PySequence_GetItem(value, i);
        if (not val) return false;

        if (not put(val, buf + i)) return false;
    }
    return true;
}


//
// Get functions. Create a python object from an epics value.
//

// Set item on dictionary and steal reference from value
bool set_item(PyObject* dict, char const* key, PyObject* value)
{
    bool result = PyDict_SetItemString(dict, key, value) == 0;
    Py_DECREF(value);
    return result;
}

PyObject* get_status(dbr_short_t const value)
{
    PyObject* args = Py_BuildValue("(h)", value);
    if (not args) return nullptr;

    PyObject* result = PyObject_Call(cac::enum_status, args, nullptr);
    Py_DECREF(args);
    return result;
}

PyObject* get_severity(dbr_short_t const value)
{
    PyObject* args = Py_BuildValue("(h)", value);
    if (not args) return nullptr;

    PyObject* result = PyObject_Call(cac::enum_severity, args, nullptr);
    Py_DECREF(args);
    return result;
}

PyObject* get_timestamp(epicsTimeStamp const value)
{
    return Py_BuildValue("(II)", value.secPastEpoch, value.nsec);
}

PyObject* get(dbr_string_t const value)
{
    return PyBytes_FromString(value);
}

template <typename T>
auto get(T const value) -> typename std::enable_if<std::is_integral<T>::value, PyObject*>::type
{
    return PyLong_FromLong(value);
}

template <typename T>
auto get(T const value) -> typename std::enable_if<std::is_floating_point<T>::value, PyObject*>::type
{
    return PyFloat_FromDouble(value);
}

template <typename T>
PyObject* get_limits(T const lower, T const upper)
{
    return Py_BuildValue("(NN)", get(lower), get(upper));
}

template <typename T>
PyObject* get_value(T const* value, long count)
{
    if (count == 1) return get(*value);

    PyObject* tuple = PyTuple_New(count);
    if (not tuple) return nullptr;

    for (long i = 0; i < count; i++) {
        PyObject* item = get(value[i]);
        if (not item) {
            Py_DECREF(tuple);
            return nullptr;
        }

        PyTuple_SET_ITEM(tuple, i, item);
    }

    return tuple;
}

template <typename T>
bool get_simple(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "value", get_value(dbrv, count)) ) return false;

    return true;
}

template <typename T>
bool get_sts(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "status",   get_status(dbrv->status))       ) return false;
    if (not set_item(dict, "severity", get_severity(dbrv->severity))   ) return false;
    if (not set_item(dict, "value",    get_value(&dbrv->value, count)) ) return false;

    return true;
}

template <typename T>
bool get_time(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "status",    get_status(dbrv->status))       ) return false;
    if (not set_item(dict, "severity",  get_severity(dbrv->severity))   ) return false;
    if (not set_item(dict, "timestamp", get_timestamp(dbrv->stamp))     ) return false;
    if (not set_item(dict, "value",     get_value(&dbrv->value, count)) ) return false;

    return true;
}

template <typename T>
bool get_enum(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "status",   get_status(dbrv->status))       ) return false;
    if (not set_item(dict, "severity", get_severity(dbrv->severity))   ) return false;
    if (not set_item(dict, "value",    get_value(&dbrv->value, count)) ) return false;

    PyObject* strs = PyTuple_New(dbrv->no_str);
    if (not strs) return false;

    for (int i = 0; i < dbrv->no_str; ++i) {
        PyObject* item = PyBytes_FromString(dbrv->strs[i]);
        if (not item) {
            Py_DECREF(strs);
            return false;
        }

        PyTuple_SET_ITEM(strs, i, item);
    }

    return set_item(dict, "enum_strings", strs);
}

template <typename T>
bool get_gr_long(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "status",         get_status(dbrv->status))       ) return false;
    if (not set_item(dict, "severity",       get_severity(dbrv->severity))   ) return false;
    if (not set_item(dict, "unit",           get(dbrv->units))               ) return false;
    if (not set_item(dict, "value",          get_value(&dbrv->value, count)) ) return false;
    if (not set_item(dict, "display_limits", get_limits(dbrv->lower_disp_limit,    dbrv->upper_disp_limit))    ) return false;
    if (not set_item(dict, "warning_limits", get_limits(dbrv->lower_warning_limit, dbrv->upper_warning_limit)) ) return false;
    if (not set_item(dict, "alarm_limits",   get_limits(dbrv->lower_alarm_limit,   dbrv->upper_alarm_limit))   ) return false;

    return true;
}

template <typename T>
bool get_gr_double(PyObject* dict, T const* dbrv, long count)
{
    if (not get_gr_long(dict, dbrv, count)) return false;
    if (not set_item(dict, "precision", get(dbrv->precision))) return false;

    return true;
}

template <typename T>
bool get_ctrl(PyObject* dict, T const* dbrv, long count)
{
    if (not set_item(dict, "control_limits", get_limits(dbrv->lower_ctrl_limit, dbrv->upper_ctrl_limit)) ) return false;

    return true;
}

template <typename T>
bool get_ctrl_long(PyObject* dict, T const* dbrv, long count)
{
    if (not get_gr_long(dict, dbrv, count)) return false;
    if (not get_ctrl(dict, dbrv, count)) return false;

    return true;
}

template <typename T>
bool get_ctrl_double(PyObject* dict, T const* dbrv, long count)
{
    if (not get_gr_double(dict, dbrv, count)) return false;
    if (not get_ctrl(dict, dbrv, count)) return false;

    return true;
}

} // namespace


// only plain values not struct types
std::pair<std::vector<uint8_t>, long> to_buffer(PyObject* value, short dbr_type, long count)
{
    std::vector<uint8_t> buffer;
    bool ok = false;
    long new_count = count;

    switch (dbr_type) {
        case DBR_STRING:
            ok = put_value<dbr_string_t>(buffer, value, new_count);
            break;
        case DBR_ENUM:
            ok = put_value<dbr_enum_t>(buffer, value, new_count);
            break;
        case DBR_CHAR:
            ok = put_value<dbr_char_t>(buffer, value, new_count);
            break;
        case DBR_SHORT:
            ok = put_value<dbr_short_t>(buffer, value, new_count);
            break;
        case DBR_LONG:
            ok = put_value<dbr_long_t>(buffer, value, new_count);
            break;
        case DBR_FLOAT:
            ok = put_value<dbr_float_t>(buffer, value, new_count);
            break;
        case DBR_DOUBLE:
            ok = put_value<dbr_double_t>(buffer, value, new_count);
            break;
        default:
            PyErr_SetString(cac::ca_exception, "Unhandeled data type");
            ok = false;
            break;
    }

    if (not ok) {
        buffer.clear();
    }
    return {std::move(buffer), new_count};
}

PyObject* from_buffer(void const* buffer, short dbr_type, long count)
{
    auto dbr = reinterpret_cast<db_access_val const*>(buffer);
    bool ok = false;

    PyObject* result = PyDict_New();
    if (not result) return nullptr;

    switch (dbr_type) {
        case DBR_STRING:
            ok = get_simple(result, &dbr->strval, count);
            break;
        case DBR_ENUM:
            ok = get_simple(result, &dbr->enmval, count);
            break;
        case DBR_CHAR:
            ok = get_simple(result, &dbr->charval, count);
            break;
        case DBR_SHORT:
            ok = get_simple(result, &dbr->shrtval, count);
            break;
        case DBR_LONG:
            ok = get_simple(result, &dbr->longval, count);
            break;
        case DBR_FLOAT:
            ok = get_simple(result, &dbr->fltval, count);
            break;
        case DBR_DOUBLE:
            ok = get_simple(result, &dbr->doubleval, count);
            break;

        case DBR_STS_STRING:
            ok = get_sts(result, &dbr->sstrval, count);
            break;
        case DBR_STS_ENUM:
            ok = get_sts(result, &dbr->senmval, count);
            break;
        case DBR_STS_CHAR:
            ok = get_sts(result, &dbr->schrval, count);
            break;
        case DBR_STS_SHORT:
            ok = get_sts(result, &dbr->sshrtval, count);
            break;
        case DBR_STS_LONG:
            ok = get_sts(result, &dbr->slngval, count);
            break;
        case DBR_STS_FLOAT:
            ok = get_sts(result, &dbr->sfltval, count);
            break;
        case DBR_STS_DOUBLE:
            ok = get_sts(result, &dbr->sdblval, count);
            break;

        case DBR_TIME_STRING:
            ok = get_time(result, &dbr->tstrval, count);
            break;
        case DBR_TIME_ENUM:
            ok = get_time(result, &dbr->tenmval, count);
            break;
        case DBR_TIME_CHAR:
            ok = get_time(result, &dbr->tchrval, count);
            break;
        case DBR_TIME_SHORT:
            ok = get_time(result, &dbr->tshrtval, count);
            break;
        case DBR_TIME_LONG:
            ok = get_time(result, &dbr->tlngval, count);
            break;
        case DBR_TIME_FLOAT:
            ok = get_time(result, &dbr->tfltval, count);
            break;
        case DBR_TIME_DOUBLE:
            ok = get_time(result, &dbr->tdblval, count);
            break;

        case DBR_GR_STRING:
            ok = get_sts(result, &dbr->gstrval, count);
            break;
        case DBR_GR_ENUM:
            ok = get_enum(result, &dbr->genmval, count);
            break;
        case DBR_GR_CHAR:
            ok = get_gr_long(result, &dbr->gchrval, count);
            break;
        case DBR_GR_SHORT:
            ok = get_gr_long(result, &dbr->gshrtval, count);
            break;
        case DBR_GR_LONG:
            ok = get_gr_long(result, &dbr->glngval, count);
            break;
        case DBR_GR_FLOAT:
            ok = get_gr_double(result, &dbr->gfltval, count);
            break;
        case DBR_GR_DOUBLE:
            ok = get_gr_double(result, &dbr->gdblval, count);
            break;

        case DBR_CTRL_STRING:
            ok = get_sts(result, &dbr->cstrval, count);
            break;
        case DBR_CTRL_ENUM:
            ok = get_enum(result, &dbr->cenmval, count);
            break;
        case DBR_CTRL_CHAR:
            ok = get_ctrl_long(result, &dbr->cchrval, count);
            break;
        case DBR_CTRL_SHORT:
            ok = get_ctrl_long(result, &dbr->cshrtval, count);
            break;
        case DBR_CTRL_LONG:
            ok = get_ctrl_long(result, &dbr->clngval, count);
            break;
        case DBR_CTRL_FLOAT:
            ok = get_ctrl_double(result, &dbr->cfltval, count);
            break;
        case DBR_CTRL_DOUBLE:
            ok = get_ctrl_double(result, &dbr->cdblval, count);
            break;

        default:
            PyErr_SetString(cac::ca_exception, "Unhandeled data type");
            ok = false;
            break;
    }

    if (not ok) {
        Py_DECREF(result);
        return nullptr;
    }

    return result;
}

} // namespace cac

#include "pv.hpp"
#include "cac.hpp"
#include "convert.hpp"

#include <vector>

#include <Python.h>
#include <structmember.h>
#include <cadef.h>

namespace cac {
namespace {

struct Pv {
    PyObject_HEAD
    char* name;                   // PV name
    PyObject* connection_handler; // connection callback
    PyObject* monitor_handler;    // monitor callback
    PyObject* access_handler;     // access rights callback
    PyObject* get_handler;        // event callback
    PyObject* put_handler;        // event callback
    chid cid;                     // channel access ID
    evid eid;                     // monitor subscription
};
static_assert(std::is_standard_layout<Pv>::value, "PV has to be standard layout to work with the Python API");

// Stores the buffer for a ca_put_callback call
struct PutData {
    Pv* pv;
    std::vector<uint8_t> buffer;
};

// Handler functions

PyDoc_STRVAR(connection_handler__doc__, R"(
Handler called when the connection state changes.

**Signature**: ``fn(connected)``

Arguments:
    connected (bool): If ``True`` the PV is now connected
)");
void connection_handler(connection_handler_args args)
{
    auto* pv = reinterpret_cast<Pv*>(ca_puser(args.chid));
    PyObject* connected = (args.op == CA_OP_CONN_UP) ? Py_True : Py_False;

    PyGILState_STATE gstate = PyGILState_Ensure();
        if (pv->connection_handler and PyCallable_Check(pv->connection_handler)) {
            PyObject* fn_args = Py_BuildValue("(O)", connected);
            if (fn_args) {
                PyObject* res = PyObject_Call(pv->connection_handler, fn_args, NULL);
                if (not res) {
                    PyErr_WriteUnraisable(pv->connection_handler);
                    PyErr_Clear();
                }
                Py_XDECREF(res);
                Py_DECREF(fn_args);
            }
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(pv));
            PyErr_Clear();
        }
    PyGILState_Release(gstate);
}

PyDoc_STRVAR(access_handler__doc__, R"(
Handler called if the access rights change.

**Signature**: ``fn(access_rights)``

Arguments:
    access_rights (:class:`AccessRights`): The new access rights.
)");
void access_handler(access_rights_handler_args args)
{
    auto* pv = reinterpret_cast<Pv*>(ca_puser(args.chid));
    int rw = args.ar.read_access ? 1 : 0;
    rw |= args.ar.write_access ? 2 : 0;

    PyGILState_STATE gstate = PyGILState_Ensure();
        if (pv->access_handler and PyCallable_Check(pv->access_handler)) {
            PyObject* fn_args = Py_BuildValue("(i)", rw);
            if (fn_args) {
                PyObject* flag = PyObject_Call(cac::flag_access_rights, fn_args, nullptr);
                Py_DECREF(fn_args);

                fn_args = Py_BuildValue("(N)", flag);
                if (fn_args) {
                    PyObject* res = PyObject_Call(pv->access_handler, fn_args, nullptr);
                    if (not res) {
                        PyErr_WriteUnraisable(pv->access_handler);
                        PyErr_Clear();
                    }
                    Py_XDECREF(res);
                    Py_DECREF(fn_args);
                }
            }
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(pv));
            PyErr_Clear();
        }
    PyGILState_Release(gstate);
}


PyDoc_STRVAR(put_handler__doc__, R"(
Handler called if the status for a put request arrives.

**Signature**: ``fn(succeeded)``

Arguments:
    succeeded (bool): If ``True`` the put request succeeded.
)");
void put_handler(event_handler_args args)
{
    PutData* data = reinterpret_cast<PutData*>(args.usr);
    Pv* pv = data->pv;
    PyObject* succeeded = (args.status == ECA_NORMAL) ? Py_True : Py_False;

    delete data; // destroy the data buffer it is not needed any more
    PyGILState_STATE gstate = PyGILState_Ensure();
        if (pv->put_handler and PyCallable_Check(pv->put_handler)) {
            PyObject* fn_args = Py_BuildValue("(O)", succeeded);
            if (fn_args) {
                PyObject* res = PyObject_Call(pv->put_handler, fn_args, nullptr);
                if (not res) {
                    PyErr_WriteUnraisable(pv->put_handler);
                    PyErr_Clear();
                }
                Py_XDECREF(res);
                Py_DECREF(fn_args);
            }
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(pv));
            PyErr_Clear();
        }
    PyGILState_Release(gstate);
}

PyDoc_STRVAR(get_handler__doc__, R"(
Handler called if the data for a get request arrives.

**Signature**: ``fn(values)``

Arguments:
    values (dict): Dictionary with the received data.
)");
void get_handler(event_handler_args args)
{
    auto* pv = reinterpret_cast<Pv*>(args.usr);

    PyGILState_STATE gstate = PyGILState_Ensure();
        if (args.status == ECA_NORMAL) {
            PyObject* result = from_buffer(args.dbr, args.type, args.count);

            if (pv->get_handler and PyCallable_Check(pv->get_handler)) {
                PyObject* fn_args = Py_BuildValue("(N)", result);
                if (fn_args) {
                    PyObject* res = PyObject_Call(pv->get_handler, fn_args, nullptr);
                    if (not res) {
                        PyErr_WriteUnraisable(pv->get_handler);
                        PyErr_Clear();
                    }
                    Py_XDECREF(res);
                    Py_DECREF(fn_args);
                }
            }
        } else {
            PyErr_Format(cac::ca_exception, "Error in get_handler for %s: %s", pv->name, ca_message(args.status));
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(pv));
            PyErr_Clear();
        }
    PyGILState_Release(gstate);
}

PyDoc_STRVAR(monitor_handler__doc__, R"(
Handler called when a subscription event is received.

**Signature**: ``fn(values)``

Arguments:
    values (dict): Dictionary with the received data.
)");
void monitor_handler(event_handler_args args)
{
    auto* pv = reinterpret_cast<Pv*>(args.usr);
    PyObject* result = nullptr;

    PyGILState_STATE gstate = PyGILState_Ensure();
        if (args.status == ECA_NORMAL) {
            result = from_buffer(args.dbr, args.type, args.count);

            if (pv->monitor_handler and PyCallable_Check(pv->monitor_handler)) {
                PyObject* fn_args = Py_BuildValue("(N)", result);
                if (fn_args) {
                    PyObject* res = PyObject_Call(pv->monitor_handler, fn_args, nullptr);
                    if (not res) {
                        PyErr_WriteUnraisable(pv->monitor_handler);
                        PyErr_Clear();
                    }
                    Py_XDECREF(res);
                    Py_DECREF(fn_args);
                }
            }
        } else {
            PyErr_Format(cac::ca_exception, "Error in get_handler for %s: %s", pv->name, ca_message(args.status));
        }

        if (PyErr_Occurred()) {
            PyErr_WriteUnraisable(reinterpret_cast<PyObject*>(pv));
            PyErr_Clear();
        }
    PyGILState_Release(gstate);
}

// Object methods

PyDoc_STRVAR(create_channel__doc__, R"(create_channel()

Create a channel for this PV.

This functions must be called  before any other channel access function
can be called.
)");
PyObject* create_channel(PyObject* self, PyObject*)
{
    constexpr int capriority = 10;
    auto* pv = reinterpret_cast<Pv*>(self);

    if (pv->cid) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is already created", pv->name);
    }

    int result = ca_create_channel(pv->name, connection_handler, self, capriority, &pv->cid);
    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Error in ca_create_channel for %s: %s", pv->name, ca_message(result));
    }

    // If an access handler was set before the channel was created set it now
    if (pv->access_handler and pv->access_handler != Py_None) {
        int result = ca_replace_access_rights_event(pv->cid, access_handler);
        if (result != ECA_NORMAL) {
            return PyErr_Format(cac::ca_exception, "Error in ca_replace_access_rights_event for %s: %s", pv->name, ca_message(result));
        }
    }

    Py_RETURN_NONE;
}

PyDoc_STRVAR(clear_channel__doc__, R"(clear_channel()

Destroy the channel of this PV.

This also removes a subscription. After this function has been called
no other channel access function can be called.
)");
PyObject* clear_channel(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (pv->cid) {
        int result;
        Py_BEGIN_ALLOW_THREADS
            result = ca_clear_channel(pv->cid);
        Py_END_ALLOW_THREADS
        if (result != ECA_NORMAL) {
            return PyErr_Format(cac::ca_exception, "Error in ca_clear_channel for %s: %s", pv->name, ca_message(result));
        }

        pv->cid = 0;
        pv->eid = 0; // clear_channel also reclaims subscriptions
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(subscribe__doc__, R"(subscribe(events, count, control, as_string)

Create a channel subscription.

If a subscription was already created it is first removed.

Arguments:
    events (:class:`Events`): Specifies which events will trigger the subscription.
    count (int): The number of elements to retrieve. If negative the number of elements of the PV is used.
    control (bool): If ``True`` the control properties (unit, limits, etc.) will be retrieved.
    as_string (bool): If ``True`` ask the server for a string representation instead of the native type.
)");
PyObject* subscribe(PyObject* self, PyObject* args)
{
    auto* pv = reinterpret_cast<Pv*>(self);
    PyObject* events;
    int count, control, as_string;

    if (not PyArg_ParseTuple(args, "Oipp:subscribe_channel", &events, &count, &control, &as_string)) return nullptr;

    PyObject* events_value = nullptr;
    if (PyLong_Check(events)) {
        Py_INCREF(events);
        events_value = events;
    } else {
        switch (PyObject_IsInstance(events, cac::flag_events)) {
          case 1:
            break;
          case 0:
            PyErr_SetString(PyExc_TypeError, "Expected Events type");
          default:
            return nullptr;
        };
        events_value = PyObject_GetAttrString(events, "value");
    }
    if (not events_value) return nullptr;

    int event_mask = PyLong_AsLong(events_value);
    Py_DECREF(events_value);
    if (PyErr_Occurred()) return nullptr;

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    int elem_count = ca_element_count(pv->cid);
    short type = ca_field_type(pv->cid);
    if (elem_count == 0 || type == TYPENOTCONN) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not connected", pv->name);
    }

    if (count < 0 or count > elem_count) {
        count = elem_count;
    }
    if (as_string) {
        type = DBF_STRING;
    }
    short dbr_type = control ? dbf_type_to_DBR_CTRL(type) : dbf_type_to_DBR_TIME(type);

    // unsubscribe first if there is already a subscription
    if (pv->eid) {
        int result = ca_clear_subscription(pv->eid);
        if (result != ECA_NORMAL) {
            return PyErr_Format(cac::ca_exception, "Error in ca_clear_subscription for %s: %s", pv->name, ca_message(result));
        }
        pv->eid = 0;
    }

    int result = ca_create_subscription(dbr_type, count, pv->cid, event_mask, monitor_handler, pv, &pv->eid);
    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Error in ca_create_subscription for %s: %s", pv->name, ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(unsubscribe__doc__, R"(unsubscribe()

Remove the subscription.
)");
PyObject* unsubscribe(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (pv->eid) {
        int result = ca_clear_subscription(pv->eid);
        if (result != ECA_NORMAL) {
            return PyErr_Format(cac::ca_exception, "Error in ca_clear_subscription for %s: %s", pv->name, ca_message(result));
        }
        pv->eid = 0;
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(set_access_handler__doc__, R"(set_access_handler(access_handler)

Change the access rights handler to :data:`access_handler`.

If the access rights change the handler is called with the new access rights.
)");
PyObject* set_access_handler(PyObject* self, PyObject* arg)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    PyObject* old_cb = pv->access_handler; // store in case of error
    Py_INCREF(arg);
    pv->access_handler = arg; // replace callback

    // if channel is connected also change ca callback
    if (pv->cid) {
        auto handler = arg == Py_None ? nullptr : access_handler;
        int result = ca_replace_access_rights_event(pv->cid, handler);
        if (result != ECA_NORMAL) {
            pv->access_handler = old_cb; // revert back to old callback
            Py_DECREF(arg);
            return PyErr_Format(cac::ca_exception, "Error in ca_replace_access_rights_event for %s: %s", pv->name, ca_message(result));
        }
    }

    Py_XDECREF(old_cb);
    Py_RETURN_NONE;
}

PyDoc_STRVAR(get_enum_strings__doc__, R"(get_enum_strings()

Request the list of enum strings from the server.

The PV must be of enum type.
)");
PyObject* get_enum_strings(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    short type = ca_field_type(pv->cid);
    if (type == TYPENOTCONN) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not connected", pv->name);
    }
    if (not dbr_type_is_ENUM(dbf_type_to_DBR(type))) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not of enum type", pv->name);
    }

    int result = ca_array_get_callback(DBR_GR_ENUM, 1, pv->cid, get_handler, pv);
    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Error in ca_array_get_callback for %s: %s", pv->name, ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(get__doc__, R"(get(count, control, as_string)

Request data from the server.

This call does not return the data but only puts the request in the
send buffer. If the data arrives the :data:`get_handler` is called with
the data.

Arguments:
    count (int): The number of elements to request. If negative the number of elements of the PV is used.
    control (bool): If ``True`` the control properties (unit, limits, etc.) will be requested.
    as_string (bool): If ``True`` ask the server for a string representation instead of the native type.
)");
PyObject* get(PyObject* self, PyObject* args)
{
    auto* pv = reinterpret_cast<Pv*>(self);
    int count, control, as_string;

    if (!PyArg_ParseTuple(args, "ipp:get_data", &count, &control, &as_string)) return nullptr;

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    int elem_count = ca_element_count(pv->cid);
    short type = ca_field_type(pv->cid);
    if (elem_count == 0 || type == TYPENOTCONN) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not connected", pv->name);
    }

    if (count < 0 or count > elem_count) {
        count = elem_count;
    }
    if (as_string) {
        type = DBF_STRING;
    }
    short dbr_type = control ? dbf_type_to_DBR_CTRL(type) : dbf_type_to_DBR_TIME(type);

    int result = ca_array_get_callback(dbr_type, count, pv->cid, get_handler, pv);
    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Error in ca_array_get_callback for %s: %s", pv->name, ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(put__doc__, R"(put(value)

Request a value change on the server.

If this call returns the data is not changed it only puts the request
for the change in the send buffer.
If the response from the server is received the :data:`put_handler` is called
with the success status.

Arguments:
    value: The new value. For array PVs a list must be used.
)");
PyObject* put(PyObject* self, PyObject* arg)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    int count = ca_element_count(pv->cid);
    short type = ca_field_type(pv->cid);
    if (count == 0 || type == TYPENOTCONN) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not connected", pv->name);
    }

    // Allow bytes for enum pvs to be send as string
    if (type == DBF_ENUM) {
      bool use_string = false;
      if (count == 1) {
          use_string = PyBytes_Check(arg);
      } else {
          PyObject* item = PySequence_GetItem(arg, 0);
          if (not item) return nullptr;
          use_string = PyBytes_Check(item);
      }
      if (use_string) type = DBF_STRING;
    }

    short dbr_type = dbf_type_to_DBR(type);
    auto buffer_result = to_buffer(arg, dbr_type, count);
    if (buffer_result.first.empty()) return nullptr;

    // The buffer must stay valid until the callback is called
    PutData* user_data = new PutData{pv, std::move(buffer_result.first)};
    int result = ca_array_put_callback(dbr_type, buffer_result.second, pv->cid, user_data->buffer.data(), put_handler, user_data);
    if (result != ECA_NORMAL) {
        delete user_data;
        return PyErr_Format(cac::ca_exception, "Error in ca_array_put_callback for %s: %s", pv->name, ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(host__doc__, R"(host()

Returns:
    str: The host string of the server.
)");
PyObject* host(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    // Interpret as UTF-8. Hopefully this is true.
    return PyUnicode_FromString(ca_host_name(pv->cid));
}

PyDoc_STRVAR(count__doc__, R"(count()

Returns:
    int: The number of elements.
)");
PyObject* count(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }
    return PyLong_FromLong(ca_element_count(pv->cid));
}

PyDoc_STRVAR(type__doc__, R"(type()

Returns:
    :class:`FieldType`: The native PV type.
)");
PyObject* type(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    short type = ca_field_type(pv->cid);
    if (type == TYPENOTCONN) {
        return PyErr_Format(cac::ca_exception, "Channel for %s is not conntected", pv->name);
    }

    PyObject* args = Py_BuildValue("(i)", type);
    if (not args) return nullptr;

    PyObject* result = PyObject_Call(cac::enum_type, args, nullptr);
    Py_DECREF(args);
    return result;
}

PyDoc_STRVAR(access__doc__, R"(access()

Returns:
    :class:`AccessRights`: The access rights.
)");
PyObject* access(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    int rw = ca_read_access(pv->cid) ? 1 : 0;
    rw |= ca_write_access(pv->cid) ? 2 : 0;

    PyObject* args = Py_BuildValue("(i)", rw);
    if (not args) return nullptr;

    PyObject* result = PyObject_Call(cac::flag_access_rights, args, nullptr);
    Py_DECREF(args);
    return result;
}

PyDoc_STRVAR(is_connected__doc__, R"(is_connected()

Returns:
    bool: ``True`` if the PV is connected.
)");
PyObject* is_connected(PyObject* self, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    if (not pv->cid) {
        return PyErr_Format(cac::ca_exception, "No channel created for %s", pv->name);
    }

    auto state = ca_state(pv->cid);

    PyObject* result = state == cs_conn ? Py_True : Py_False;
    Py_INCREF(result);
    return result;
}

int pv_init(PyObject* self, PyObject* args, PyObject*)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    char const *c_name;
    if (not PyArg_ParseTuple(args, "s", &c_name)) return -1;

    pv->name = strdup(c_name);
    pv->connection_handler = nullptr;
    pv->monitor_handler = nullptr;
    pv->access_handler = nullptr;
    pv->get_handler = nullptr;
    pv->put_handler = nullptr;
    pv->cid = 0;
    pv->eid = 0;

    return 0;
}

void pv_dealloc(PyObject* self)
{
    auto* pv = reinterpret_cast<Pv*>(self);

    free(pv->name);
    Py_XDECREF(pv->connection_handler);
    Py_XDECREF(pv->monitor_handler);
    Py_XDECREF(pv->access_handler);
    Py_XDECREF(pv->get_handler);
    Py_XDECREF(pv->put_handler);
    if (pv->cid) {
        ca_clear_channel(pv->cid);
        pv->cid = 0;
        pv->eid = 0;
    }

    Py_TYPE(self)->tp_free(self);
}

PyObject* pv_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    PyObject* self = type->tp_alloc(type, 0);
    if (not self) {
        PyErr_SetString(PyExc_RuntimeError, "Could not allocate new PV");
        return nullptr;
    }

    return self;
}

PyMethodDef pv_methods[] = {
    {"create_channel",      create_channel,     METH_NOARGS,  create_channel__doc__},
    {"clear_channel",       clear_channel,      METH_NOARGS,  clear_channel__doc__},
    {"subscribe",           subscribe,          METH_VARARGS, subscribe__doc__},
    {"unsubscribe",         unsubscribe,        METH_NOARGS,  unsubscribe__doc__},
    {"get",                 get,                METH_VARARGS, get__doc__},
    {"put",                 put,                METH_O,       put__doc__},
    {"get_enum_strings",    get_enum_strings,   METH_NOARGS,  get_enum_strings__doc__},
    {"host",                host,               METH_NOARGS,  host__doc__},
    {"count",               count,              METH_NOARGS,  count__doc__},
    {"type",                type,               METH_NOARGS,  type__doc__},
    {"access",              access,             METH_NOARGS,  access__doc__},
    {"set_access_handler",  set_access_handler, METH_O,       set_access_handler__doc__},
    {"is_connected",        is_connected,       METH_NOARGS,  is_connected__doc__},
    {nullptr},
};

PyMemberDef pv_members[] = {
    {"name",               T_STRING,    offsetof(Pv, name),               1, "PV name"}, // READONLY
    {"connection_handler", T_OBJECT_EX, offsetof(Pv, connection_handler), 0, connection_handler__doc__},
    {"monitor_handler",    T_OBJECT_EX, offsetof(Pv, monitor_handler),    0, monitor_handler__doc__},
    {"access_handler",     T_OBJECT_EX, offsetof(Pv, access_handler),     1, access_handler__doc__}, // READONLY
    {"get_handler",        T_OBJECT_EX, offsetof(Pv, get_handler),        0, get_handler__doc__},
    {"put_handler",        T_OBJECT_EX, offsetof(Pv, put_handler),        0, put_handler__doc__},
    {nullptr}
};

PyDoc_STRVAR(pv_type__doc__, R"(PV(name)

Create a new process value with name ``name``.

This class encapsulates a channel id and forwards the callback functions
to python.

Some of the API functionality is not implemented:

    - Multiple subscriptions
    - Non-callback get and put
    - Synchronous groups

The following keys can occur in a values dictionary:

value
    Data value, type depends on the native type. For integer types
    and enum types this is ``int``, for floating point types this is ``float``.
    For string types this is ``bytes`` because there is no defined encoding.

status
    Value status, one of :class:`Status`.

severity
    Value severity, one of :class:`Severity`.

timestamp
    An epics timestamp tule ``(seconds, nanoseconds)``. The time is
    counted from the epics epoch.

enum_strings
    Tuple with the strings corresponding to the enumeration values.
    The entries are ``bytes`` because there is not defined encoding.

unit
    String representing the physical unit of the value. The type is
    ``bytes`` because there is not defined encoding.

precision
    Integer representing the number of relevant decimal places.

display_limits
    A tuple ``(minimum, maximum)`` representing the range of values
    for a user interface.

control_limits
    A tuple ``(minimum, maximum)`` representing the range of values
    accepted for a put request by the server.

warning_limits
    A tuple ``(minimum, maximum)``. When the value lies outside of the
    range of values the status becomes :class:`Status.LOW` or :class:`Status.HIGH`.

alarm_limits
    A tuple ``(minimum, maximum)``. When the value lies outside of the
    range of values the status becomes :class:`Status.LOLO` or :class:`Status.HIHI`.
)");

PyTypeObject pv_type = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "ca_client.cac.PV",                        /* tp_name */
    sizeof(Pv),                                /* tp_basicsize */
    0,                                         /* tp_itemsize */
    pv_dealloc,                                /* tp_dealloc */
    nullptr,                                   /* tp_print */
    nullptr,                                   /* tp_getattr */
    nullptr,                                   /* tp_setattr */
    nullptr,                                   /* tp_as_async */
    nullptr,                                   /* tp_repr */
    nullptr,                                   /* tp_as_number */
    nullptr,                                   /* tp_as_sequence */
    nullptr,                                   /* tp_as_mapping */
    nullptr,                                   /* tp_hash */
    nullptr,                                   /* tp_call */
    nullptr,                                   /* tp_str */
    nullptr,                                   /* tp_getattro */
    nullptr,                                   /* tp_setattro */
    nullptr,                                   /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /* tp_flags */
    pv_type__doc__,                            /* tp_doc */
    nullptr,                                   /* tp_traverse */
    nullptr,                                   /* tp_clear */
    nullptr,                                   /* tp_richcompare */
    0,                                         /* tp_weaklistoffset */
    nullptr,                                   /* tp_iter */
    nullptr,                                   /* tp_iternext */
    pv_methods,                                /* tp_methods */
    pv_members,                                /* tp_members */
    nullptr,                                   /* tp_getset */
    nullptr,                                   /* tp_base */
    nullptr,                                   /* tp_dict */
    nullptr,                                   /* tp_descr_get */
    nullptr,                                   /* tp_descr_set */
    0,                                         /* tp_dictoffset */
    pv_init,                                   /* tp_init */
    nullptr,                                   /* tp_alloc */
    pv_new,                                    /* tp_new */
};

} // namespace

PyObject* create_pv_type()
{
    if (PyType_Ready(&pv_type) < 0) return nullptr;

    Py_INCREF(&pv_type);
    return reinterpret_cast<PyObject*>(&pv_type);
}

void destroy_pv_type()
{
    Py_DECREF(&pv_type);
}

} // namespace cac

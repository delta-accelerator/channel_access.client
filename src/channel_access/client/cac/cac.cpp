#include "cac.hpp"
#include "pv.hpp"

#include <Python.h>

#include <cadef.h>

namespace cac {

PyObject* ca_exception = nullptr;

PyObject* flag_access_rights = nullptr;
PyObject* flag_events = nullptr;
PyObject* enum_type = nullptr;
PyObject* enum_status = nullptr;
PyObject* enum_severity = nullptr;


namespace {

// The current client context for this process
ca_client_context* client_context = nullptr;

PyDoc_STRVAR(attach_context__doc__, R"(attach_context()

Attach the current thread to the process wide ca context.

This must be called by all non-main threads who will use the channel access library.
)");
PyObject* attach_context(PyObject*, PyObject*)
{
    // only failure modes are if it's already attached or single threaded,
    // so no need to raise an exception
    if (ca_current_context() == nullptr) {
        if (not client_context) {
            PyErr_SetString(cac::ca_exception, "No current ca context. initialize() not called?");
            return nullptr;
        }

        int result = ca_attach_context(client_context);
        if (result != ECA_NORMAL) {
            return PyErr_Format(cac::ca_exception, "Could not attach to ca context: %s", ca_message(result));
        }
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(detach_context__doc__, R"(detach_context()

Detach the current thread from the process wide ca context.
)");
PyObject* detach_context(PyObject*, PyObject*)
{
    ca_detach_context();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(pend_io__doc__, R"(pend_io(timeout)

Flush the send buffer and wait for outstandig get requests.

This function flushes the send buffer and then blocks until outstanding
get requests complete..

Arguments:
    timeout (float): Timeout interval in seconds. 0 means forever.
)");
PyObject* pend_io(PyObject*, PyObject* arg)
{
    double timeout = PyFloat_AsDouble(arg);
    if (PyErr_Occurred()) return nullptr;

    int result;
    Py_BEGIN_ALLOW_THREADS
        result = ca_pend_io(timeout);
    Py_END_ALLOW_THREADS

    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Could not execute pend_io: %s", ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(flush_io__doc__, R"(flush_io()

Flush outstanding IO requests to the server.
)");
PyObject* flush_io(PyObject*, PyObject*)
{
    int result = ca_flush_io();

    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Could not execute flush_io: %s", ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(pend_event__doc__, R"(pend_event(timeout)

Flush the send buffer and process CA background activity.

This function flushes the send buffer and then blocks for ``timeout``
seconds processing CA background activity.

Arguments:
    timeout (float): Timeout interval in seconds. 0 means forever.
)");
PyObject* pend_event(PyObject*, PyObject* arg)
{
    double timeout = PyFloat_AsDouble(arg);
    if (PyErr_Occurred()) return nullptr;

    int result;
    Py_BEGIN_ALLOW_THREADS
        result = ca_pend_event(timeout);
    Py_END_ALLOW_THREADS

    if (result != ECA_TIMEOUT) {
        return PyErr_Format(cac::ca_exception, "Could not execute pend_event: %s", ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(poll__doc__, R"(poll()

Flush the send buffer and process CA background activity.
)");
PyObject* poll(PyObject*, PyObject*)
{
    int result;
    Py_BEGIN_ALLOW_THREADS
        result = ca_poll();
    Py_END_ALLOW_THREADS

    if (result != ECA_TIMEOUT) {
        return PyErr_Format(cac::ca_exception, "Could not execute poll: %s", ca_message(result));
    }
    Py_RETURN_NONE;
}

PyDoc_STRVAR(initialize__doc__, R"(initialize(preemptive)

Create a process wide ca context.

This must be called by the main thread to initialize the channel access library.

Arguments:
    preemptive (bool): If ``True`` preemptive callback mode is enabled.
)");
PyObject* initialize(PyObject* dummy, PyObject* arg)
{
    // libca creates non-python threads so we must ensure that python
    // threading is initialized
    PyEval_InitThreads();

    auto preemptive = PyObject_IsTrue(arg) ? ca_enable_preemptive_callback : ca_disable_preemptive_callback;

    int result = ca_context_create(preemptive);
    if (result != ECA_NORMAL) {
        return PyErr_Format(cac::ca_exception, "Could not create ca context: %s", ca_message(result));
    }
    client_context = ca_current_context();
    Py_RETURN_NONE;
}

PyDoc_STRVAR(finalize__doc__, R"(

Destroy the process wide ca context.

This should be called to shutdown the channel access library.
)");
PyObject* finalize(PyObject*, PyObject*)
{
    client_context = nullptr;
    ca_context_destroy();
    Py_RETURN_NONE;
}

// Register module methods
PyMethodDef methods[] = {
    {"attach_context", attach_context, METH_NOARGS, attach_context__doc__},
    {"detach_context", detach_context, METH_NOARGS, detach_context__doc__},
    {"pend_io",        pend_io,        METH_O,      pend_io__doc__},
    {"flush_io",       flush_io,       METH_NOARGS, flush_io__doc__},
    {"pend_event",     pend_event,     METH_O,      pend_event__doc__},
    {"poll",           poll,           METH_NOARGS, poll__doc__},
    {"initialize",     initialize,     METH_O,      initialize__doc__},
    {"finalize",       finalize,       METH_NOARGS, finalize__doc__},
    {nullptr}
};

PyDoc_STRVAR(cac__doc__, R"(
Low level wrapper module over the *libca* interface.
)");
PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "channel_access.client.cac",  /* name of module */
    cac__doc__,                   /* module documentation, may be NULL */
    -1,                           /* size of per-interpreter state of the module,
                                     or -1 if the module keeps state in global variables. */
    methods,                      /* methods */
};


PyDoc_STRVAR(ca_exception__doc__, R"(
This exception is raised when a channel access functions returns an error.
)");
PyObject* add_exception(PyObject* module)
{
    PyObject* exception = PyErr_NewExceptionWithDoc("channel_access.client.cac.CaException", ca_exception__doc__, PyExc_RuntimeError, nullptr);
    if (not exception) return nullptr;

    Py_INCREF(exception);
    if (PyModule_AddObject(module, "CaException", exception) != 0) {
        Py_DECREF(exception);
        return nullptr;
    }

    return exception;
}

} // namespace
} // namespace cac

extern "C" {

PyMODINIT_FUNC PyInit_cac(void)
{
    int result;

    PyObject* module = nullptr, *pv_type = nullptr;
    PyObject* ca_module = nullptr;

    module = PyModule_Create(&cac::module);
    if (not module) goto error;

    cac::ca_exception = cac::add_exception(module);
    if (not cac::ca_exception) goto error;


    ca_module = PyImport_ImportModule("channel_access.common");
    if (not ca_module) goto error;

    cac::flag_access_rights = PyObject_GetAttrString(ca_module, "AccessRights");
    if (not cac::flag_access_rights) goto error;

    cac::flag_events = PyObject_GetAttrString(ca_module, "Events");
    if (not cac::flag_events) goto error;

    cac::enum_type = PyObject_GetAttrString(ca_module, "Type");
    if (not cac::enum_type) goto error;

    cac::enum_status = PyObject_GetAttrString(ca_module, "Status");
    if (not cac::enum_status) goto error;

    cac::enum_severity = PyObject_GetAttrString(ca_module, "Severity");
    if (not cac::enum_severity) goto error;


    pv_type = cac::create_pv_type();
    if (not pv_type) goto error;

    result = PyModule_AddObject(module, "PV", pv_type);
    pv_type = nullptr;
    if (result != 0) {
        PyErr_SetString(PyExc_RuntimeError, "Could not add PV class");
        goto error;
    }

    Py_DECREF(ca_module);
    return module;

error:
    Py_XDECREF(pv_type);
    Py_XDECREF(cac::enum_severity);
    Py_XDECREF(cac::enum_status);
    Py_XDECREF(cac::enum_type);
    Py_XDECREF(cac::flag_events);
    Py_XDECREF(cac::flag_access_rights);
    Py_XDECREF(ca_module);
    Py_XDECREF(cac::ca_exception);
    Py_XDECREF(module);
    return nullptr;
}

}

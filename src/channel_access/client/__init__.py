"""
High level implementation of a channel access client.
"""
import weakref
import threading
from datetime import datetime
import enum

import channel_access.common as ca
from . import cac
from .cac import CaException



# Can't distinguish between None value and timeout
class EventData(object):
    def __init__(self, default=None):
        self._cond = threading.Condition()
        self._default = default
        self._value = None

    def set(self, value):
        with self._cond:
            self._value = value
            self._cond.notify()

    def get(self, timeout=None):
        result = None
        with self._cond:
            if self._value is None:
                self._cond.wait(timeout)
            result = self._value
            self._value = None
        return result


class HandlerSet(object):
    """ Thread-safe set of handler functions.

    Allows handler to be added and removed while iterating. The changes
    are defered until the next iteration.
    """
    def __init__(self):
        self._added_lock = threading.Lock()
        self._added = set()
        self._removed_lock = threading.Lock()
        self._removed = set()
        self._active = set()

    def add(self, handler):
        with self._added_lock:
            self._added.add(handler)

    def remove(self, handler):
        with self._removed_lock:
            self._removed.add(handler)

    def run(self, *args, **kwargs):
        with self._added_lock, self._removed_lock:
            self._active |= self._added
            self._added.clear()
            self._active -= self._removed
            self._removed.clear()
        for handler in self._active:
            handler(*args, **kwargs)



class InitData(enum.Enum):
    """ Data to initialize when a PV connects. """
    NONE = 0
    DATA = 1
    CONTROL = 2


class PV(object):
    """
    A channel access PV.

    This class gives thread-safe access to a channel access PV.

    It implements the context manager interface which makes it possible
    to use a with-statement to guarantee disconnect.

    The default parameters are such that methods will block until the
    request is fullfilled and then return the result.

    When calling methods on many PVs it is faster to use the non-blocking
    versions and at the end call :meth:`Client.flush()`.

    The following keys may occur in the values dictionaries:

        * timestamp
        * status
        * severity
        * value
        * precision
        * unit
        * enum_strings
        * display_limits
        * control_limits
        * warning_limits
        * alarm_limits
    """
    def __init__(self, name, connect=True, monitor=True,
                 initialize=InitData.CONTROL, encoding='utf-8'):
        """
        Arguments:
            name (str): Name of the remote PV.
            connect (bool|callable):
                If ``True`` automatically call ``connect(block=False)`` after creating the PV.
                If ``False`` :meth:`connect()` must be called to use
                any channel access methods.

                If this is a callable, add it as a connection handler and
                automatically call ``connect(block=False)`` after creating the PV.
            monitor (bool|callable):
                If ``True`` automatically subscribe after :meth:`connect` is called.

                If this is a callable, add it as a monitor handler and
                automatically subscribe after :meth:`connect` is called.
            initialize (:class:`InitData`):
                If ``InitData.DATA`` automatically call ``get(block=False)``
                after a connection is etablished.

                If ``InitData.CONTROL`` automatically call ``get(block=False, control=True)``
                after a connection is etablished.

                This automatically initializes the PV data with the remote values
                as soon as a connection is etablished.
            encoding (str):
                The string encoding used for units, enum strings and string values.

                If ``None`` these values are ``bytes`` instead of ``str`` objects.
        """
        self._encoding = encoding
        self._auto_initialize = initialize
        self._auto_monitor = bool(monitor)

        self._connect_value = EventData(False)
        self._get_value = EventData()
        self._put_value = EventData()
        self._subscribed = False

        self._data = {}
        self._data_lock = threading.Lock()

        self._connection_handlers = HandlerSet()
        self._monitor_handlers = HandlerSet()

        self._pv = cac.PV(name)
        self._pv.connection_handler = self._connection_handler
        self._pv.put_handler = self._put_handler
        self._pv.get_handler = self._get_handler
        self._pv.monitor_handler = self._monitor_handler

        if connect and not isinstance(connect, bool):
            self.add_connection_handler(connect)

        if monitor and not isinstance(monitor, bool):
            self.add_monitor_handler(monitor)

        if connect:
            self.connect(block=False)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.disconnect()

    def __del__(self):
        try:
            self.disconnect()
        except cac.CaException:
            pass

    def _decode(self, values):
        result = values.copy()

        if self._encoding is not None:
            if 'unit' in result:
                result['unit'] = result['unit'].decode(self._encoding)

            if 'enum_strings' in result:
                result['enum_strings'] = tuple(x.decode(self._encoding) for x in result['enum_strings'])

            if 'value' in result and isinstance(result['value'], bytes):
                result['value'] = result['value'].decode(self._encoding)

        if 'timestamp' in result:
            result['timestamp'] = ca.epics_to_datetime(result['timestamp'])

        return result

    def _connection_handler(self, connected):
        self._connect_value.set(connected)
        if connected:
            if self._auto_initialize == InitData.DATA:
                self.get(block=False, control=False)
            elif self._auto_initialize == InitData.CONTROL:
                self.get(block=False, control=True)
                if self.is_enum:
                    self.get_enum_strings(block=False)
            if self._auto_monitor:
                self.subscribe()
            if self._auto_monitor or self._auto_initialize != InitData.NONE:
                cac.flush_io()
        self._connection_handlers.run(self, connected)

    def _put_handler(self, succeeded):
        self._put_value.set(succeeded)

    def _get_handler(self, values):
        values = self._decode(values)
        self._get_value.set(values)
        with self._data_lock:
            self._data.update(values)
        self._monitor_handlers.run(self, values)

    def _monitor_handler(self, values):
        values = self._decode(values)
        with self._data_lock:
            self._data.update(values)
        self._monitor_handlers.run(self, values)

    def add_connection_handler(self, handler):
        """
        Add a connection handler.

        The connection handlers are called when the connection status
        of the PV changes.

        **Handler**:
            **Signature**: ``fn(pv, connected)``

            **Parameters**:

                * **pv** (:class:`PV`): The :class:`PV` object with the
                  changed connection state.
                * **connected** (bool): If ``True`` the PV is connected.


        Arguments:
            handler (callable): The connection handler to add.
        """
        self._connection_handlers.add(handler)

    def remove_connection_handler(self, handler):
        """
        Remove a connection handler.

        Arguments:
            handler (callable): The same object used in :meth:`add_connection_handler`.
        """
        self._connection_handlers.remove(handler)

    def add_monitor_handler(self, handler):
        """
        Add a monitor handler.

        The monitor handlers are called when a channel access
        subscription triggers.

        **Handler**:
            **Signature**: ``fn(pv, values)``

            **Parameters**:

                * **pv** (:class:`PV`): The :class:`PV` object with the
                  changed values.
                * **values** (dict): A dictionary with the changed values.


        Arguments:
            handler (callable): The monitor handler to add.
        """
        self._monitor_handlers.add(handler)

    def remove_monitor_handler(self, handler):
        """
        Remove a monitor handler.

        Arguments:
            handler (callable): The same object used in :meth:`add_monitor_handler`.
        """
        self._monitor_handlers.remove(handler)

    def connect(self, block=True):
        """
        Create the channel and try to connect to it.

        Arguments:
            block (bool|float):
                If ``True`` block until a connection is etablished.
                If ``False`` the method returns immediatly.

                If this is a float, block for at most ``block`` seconds
                and return wether the connection is etablished or not.

        Returns:
            * ``None`` if ``block == False``.
            * ``True`` if ``block == True``.
            * If ``block > 0.0`` wether the connection is etablished.
        """
        self._pv.create_channel()

        if block is True or block > 0:
            cac.flush_io()
            return self._connect_value.get(None if block is True else block)

    def disconnect(self):
        """
        Destroy the channel.

        This also removes any active subscriptions. After this method is
        called no other channel access methods can be called.
        """
        self._pv.clear_channel()

    def ensure_connected(self, timeout=None):
        """
        Ensure that a connection is etablished.

        Arguments:
            timeout (None|float):
                If ``None`` block until a connection is etablished.

                If this is a float wait at most for ``timeout`` seconds.

        Raises:
            RuntimeError: If the connection could not be etablished.

        """
        try:
            connected = self.connected
        except cac.CaException:
            connected = False
            self.connect(block=False)
            cac.flush_io()

        if not connected:
            connected = self._connect_value.get(timeout)

        if not connected:
            raise RuntimeError("Could not ensure connection")

    def subscribe(self, trigger = ca.Events.VALUE | ca.Events.ALARM , count=None, control=False, as_string=False):
        """
        Create a channel access subscription.

        The request for the subscription is only queued. :meth:`Client.flush()`
        must be called to ensure it is send to the server.

        Arguments:
            trigger (:class:`Trigger`):
                The trigger sources for this subscription. See :class:`Trigger`.
            count (None|int):
                If ``None`` use the element count of this PV. Otherwise
                request ``count`` elements from the server.
            control (bool):
                If ``True`` request control values
                (precision, unit, limits, etc.) from the server.
            as_string (bool):
                If ``True`` request values as formatted strings from the server.
        """
        if count is None:
            count = self.count

        self._pv.subscribe(trigger, count, control, as_string)
        self._subscribed = True

    def unsubscribe(self):
        """
        Remove the channel access subscription.
        """
        self._subscribed = False
        self._pv.unsubscribe()

    def put(self, value, block=True):
        """
        Write a value into the PV.

        If ``block == False`` a request is only queued. :meth:`Client.flush()`
        must be called to ensure it is send to the server.

        Arguments:
            value: The new value. For array PVs a list must be used.
            block (bool|float):
                If ``True`` block until the value is changed on the server.
                If ``False`` the method returns immediatly.

                If this is a float, block for at most ``block`` seconds
                and return wether the value is changed on the server
                or ``None`` if the timeout occured.

        Returns:
            * ``None`` if ``block == False``.
            * ``True`` if ``block == True``.
            * If ``block > 0.0`` wether the value is changed on the server
              or ``None`` if the timeout occured.
        """
        if isinstance(value, str):
            if self._encoding is None:
                raise TypeError("str value not allowed if no encoding is used")
            value = value.encode(self._encoding)
        self._pv.put(value)

        if block is True or block > 0:
            cac.flush_io()
            return self._put_value.get(None if block is True else block)
        return None

    def get(self, block=True, count=None, control=False, as_string=False):
        """
        Read a value from the server.

        If ``block == False`` a request is only queued. :meth:`Client.flush()`
        must be called to ensure it is send to the server.

        Arguments:
            block (bool|float):
                If ``True`` block until the value arrived from the server.
                If ``False`` the method returns immediatly.

                If this is a float, block for at most ``block`` seconds
                and return the value or ``None`` if the timeout occured.
            count (None|int):
                If ``None`` use the element count of this PV. Otherwise
                request ``count`` elements from the server.
            control (bool):
                If ``True`` request control values
                (precision, unit, limits, etc.) from the server.
            as_string (bool):
                If ``True`` request values as formatted strings from the server.

        Returns:
            * ``None`` if ``block == False``.
            * The value if ``block == True``.
            * If ``block > 0.0`` the value or ``None`` if the timeout occured.
        """
        if count is None:
            count = self.count

        self._pv.get(count, control, as_string)

        if block is True or block > 0:
            cac.flush_io()
            data = self._get_value.get(None if block is True else block)
            if data is not None:
                return data.get('value')
        return None

    def get_enum_strings(self, block=True):
        """
        Read the tuple of enumeration strings form the server.

        If ``block == False`` a request is only queued. :meth:`Client.flush()`
        must be called to ensure it is send to the server.

        Arguments:
            block (bool|float):
                If ``True`` block until the value arrived from the server.
                If ``False`` the method returns immediatly.

                If this is a float, block for at most ``block`` seconds
                and return the strings or ``None`` if the timeout occured.

        Returns:
            * ``None`` if ``block == False``.
            * The strings if ``block == True``.
            * If ``block > 0.0`` the strings or ``None`` if the timeout occured.
        """
        self._pv.get_enum_strings()

        if block is True or block > 0:
            cac.flush_io()
            data = self._get_value.get(None if block is True else block)
            if data is not None:
                return data.get('enum_strings')
        return None

    @property
    def name(self):
        """
        str: The name of this PV.
        """
        return self._pv.name

    @property
    def host(self):
        """
        str: The remote host of this PV.
        """
        return self._pv.host()

    @property
    def count(self):
        """
        int: The number of elements of this PV.
        """
        return self._pv.count()

    @property
    def type(self):
        """
        :class:`FieldType`: The data type of this PV.
        """
        return self._pv.type()

    @property
    def access_rights(self):
        """
        :class:`AccessRights`: The access rights to this PV.
        """
        return self._pv.access()

    @property
    def connected(self):
        """
        bool: Wether this PV is connected or not.
        """
        return self._pv.is_connected()

    @property
    def monitored(self):
        """
        bool: Wether this PV is monitored by a :func:`subscribe` call.
        """
        return self._subscribed

    @property
    def is_enum(self):
        """
        bool: Wether this PV is of enumeration type.
        """
        return self.type == ca.Type.ENUM

    @property
    def data(self):
        """
        dict: A dictionary with the current values.
        """
        with self._data_lock:
            # We need a copy here for thread-safety. All keys and values
            # are immutable so a shallow copy is enough
            return self._data.copy()

    @property
    def timestamp(self):
        """
        datetime: The timestamp in UTC of the last received data or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('timestamp')

    @property
    def value(self):
        """
        The current value of the PV or ``None`` if it's unknown.

        This is writeable and calls ``put(value, block=False)``.
        """
        with self._data_lock:
            return self._data.get('value')

    @value.setter
    def value(self, value):
        self.put(value, block=False)

    @property
    def valid_value(self, exception=True):
        """
        Return a valid value or throw an exception.

        This property will only return a value if the pv is connected,
        it's severity is not INVALID and a value was received before.
        """
        if not self._pv.is_connected():
            raise RuntimeError("PV is not connected")
        with self._data_lock:
            value = self._data.get('value')
            severity = self._data.get('severity')
        if severity == Severity.INVALID:
            raise RuntimeError("PV value is invalid")
        if value is None:
            raise RuntimeError("PV value is unknown")
        return value

    @property
    def status(self):
        """
        :class:`Status`: The current status or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('status')

    @property
    def severity(self):
        """
        :class:`Severity`: The current severity or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('severity')

    @property
    def precision(self):
        """
        int: The current precision or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('precision')

    @property
    def unit(self):
        """
        str|bytes: The current unit or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('unit')

    @property
    def enum_strings(self):
        """
        tuple(str|bytes): The current enumeration strings or ``None`` if it's unknown.
        """
        with self._data_lock:
            return self._data.get('enum_strings')

    @property
    def display_limits(self):
        """
        tuple(float, float): The current display limits or ``None`` if they are unknown.
        """
        with self._data_lock:
            return self._data.get('display_limits')

    @property
    def control_limits(self):
        """
        tuple(float, float): The control display limits or ``None`` if they are unknown.
        """
        with self._data_lock:
            return self._data.get('control_limits')

    @property
    def warning_limits(self):
        """
        tuple(float, float): The warning display limits or ``None`` if they are unknown.
        """
        with self._data_lock:
            return self._data.get('warning_limits')

    @property
    def alarm_limits(self):
        """
        tuple(float, float): The alarm display limits or ``None`` if they are unknown.
        """
        with self._data_lock:
            return self._data.get('alarm_limits')


class Client(object):
    """
    Channel Access client.

    This class manages the process wide channel access context.
    An instance of this class must be created before any PV can be created.
    It must also be :meth:`shutdown()` before the process ends.

    It implements the context manager interface which makes it possible
    to use a with-statement to guarantee shutdown.

    A preemptive context is used so no polling functions have to be called.
    Depending on the use case :meth:`flush` has to be called.
    """
    def __init__(self):
        super().__init__()
        self._pvs = weakref.WeakValueDictionary()

        cac.initialize(True)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.shutdown()

    def flush(self, timeout=None):
        """
        Flush any outstanding server requests.

        When using the non-blocking methods of the :class:`PV` class, requests
        are only queued but not necessarily send to the server.
        Calling this funtions flushes the send queue.

        Arguments:
            timeout (float): If ``None`` don't block, otherwise block
                             for ``timeout`` seconds.
        """
        if timeout is not None:
            cac.pend_event(timeout)
        else:
            cac.flush_io()

    def shutdown(self):
        """
        Shutdown the channel access client.

        Destroy the process wide channel access context and
        disconnect all active PV objects.
        """
        for pv in self._pvs.values():
            pv.disconnect()
        cac.flush_io()
        cac.finalize()

    def createPV(self, name, *args, **kwargs):
        """
        Create a new channel access PV.

        All arguments are forwarded to the :class:`PV` class.

        PV objects are cached. Calling this function multiple times
        with the same name will return the same object if it is not
        garbage collected between the calls.

        Returns:
            :class:`PV`: A new PV object.
        """
        pv = self._pvs.get(name)
        if pv is None:
            pv = PV(name, *args, **kwargs)
            self._pvs[name] = pv
        return pv

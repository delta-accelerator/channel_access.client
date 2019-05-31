Channel Access client library
=============================
This library contains the low-level bindings to the *libca* library and an
high-level thread-safe interface for ease of use.

For the server implementation see `channel_access.server`_.

.. _channel_access.server: https://pypi.org/project/channel_access.server

Installation
------------
Before installing the library, the environment variables ``EPICS_BASE``
and ``EPICS_HOST_ARCH`` must be set.

Then the library can be installed with pip::

    pip install channel_access.client

Get the source
--------------
The source code is available in a `Github repository`_::

    git clone https://github.com/delta-accelerator/channel_access.client

.. _Github repository: https://github.com/delta-accelerator/channel_access.client

Documentation
-------------
The documentation can be generated from the source code with *sphinx*::

    cd /path/to/repository
    pip install -e .
    python setup.py build_sphinx

Then open ``build/sphinx/html/index.html``.

Tests
-----
Tests are run with *pytest*::

    cd /path/to/repository
    pytest -v

To run the tests for all supported version use *tox*::

    cd /path/to/repository
    tox

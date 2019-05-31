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

Examples
--------
Examples are located in the ``examples`` directory.

The ``simple.py`` example monitors a single process value and outputs the
contents of the data dictionary::

    python examples/simple.py test-pv

Get the source
--------------
The source code is available in a `Github repository`_::

    git clone https://github.com/delta-accelerator/channel_access.client

.. _Github repository: https://github.com/delta-accelerator/channel_access.client

Documentation
-------------
The documentation for the last version is available `online`_.

The documentation can also be generated from the source code with *sphinx*.
The package must be installed prior to building the documentation::

    cd /path/to/repository
    pip install .
    python setup.py build_sphinx

Then open ``build/sphinx/html/index.html``.

.. _online: https://delta-accelerator.github.io/channel_access.client

Tests
-----
Tests are run with *pytest*::

    cd /path/to/repository
    pytest -v

To run the tests for all supported version use *tox*::

    cd /path/to/repository
    tox

import pytest
import datetime

import channel_access.common as ca
import channel_access.client as cac
from . import common



def test_attribute_count(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, count=None)
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)

    assert(c_pv.count == None)

def test_attribute_count(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, count=5)
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)

    assert(c_pv.count == 5)

def test_attribute_type(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG)
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)

    assert(c_pv.type == ca.Type.LONG)

def test_attribute_connected(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG)
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)

    assert(c_pv.connected)

def test_attribute_is_enum(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.ENUM)
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)

    assert(c_pv.is_enum)

def test_attribute_timestamp(server, client):
    now = datetime.datetime(2005, 5, 3, 3, 54, 59, tzinfo=datetime.timezone.utc)
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        'timestamp': now
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=False)

    assert(c_pv.timestamp == now)

def test_attribute_value(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        'value': 42
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(c_pv.value == 42)
    assert(c_pv.valid_value == 42)

def test_attribute_status(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        'status': ca.Status.UDF,
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(c_pv.status == ca.Status.UDF)

def test_attribute_severity(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        'severity': ca.Severity.INVALID,
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(c_pv.severity == ca.Severity.INVALID)

def test_attribute_precision(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.FLOAT, attributes={
        'precision': 3,
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(c_pv.precision == 3)

def test_attribute_unit(server, client):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        'unit': 'test',
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(c_pv.unit == 'test')

@pytest.mark.parametrize("limits", ['display_limits', 'control_limits', 'warning_limits', 'alarm_limits'])
def test_attribute_limits(server, client, limits):
    s_pv = server.createPV('CAS:Test', ca.Type.LONG, attributes={
        limits: (-42, 23)
    })
    c_pv = client.createPV('CAS:Test', monitor=False, initialize=cac.InitData.NONE)
    c_pv.ensure_connected(timeout=0.1)
    c_pv.get(block=0.1, control=True)

    assert(getattr(c_pv, limits) == (-42, 23))

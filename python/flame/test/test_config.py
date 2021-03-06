from __future__ import print_function

import unittest
import logging
from collections import OrderedDict

import os
datadir = os.path.dirname(__file__)

from numpy import asarray
from numpy.testing import assert_array_almost_equal as assert_array_equal
from numpy.testing import assert_equal

from .._internal import (GLPSPrinter as dictshow,
                         FLAME_ERROR, FLAME_WARN, FLAME_INFO, FLAME_DEBUG, setLogLevel, getLoggerName)
from .. import GLPSParser, Machine
import os
datadir = os.path.dirname(__file__)

class testPrint(unittest.TestCase):
    def test_unicode(self):
        P = dictshow({ 'elements':[{ 'name':'drift_1', 'type':'drift' }] })
        self.assertEqual(P, 'drift_1: drift;\ndefault: LINE = (drift_1);\nUSE: default;\n')

        P = dictshow({ 'elements':[{ 'name':u'drift_1', 'type':u'drift' }] })
        self.assertEqual(P, 'drift_1: drift;\ndefault: LINE = (drift_1);\nUSE: default;\n')

class testParse(unittest.TestCase):
    maxDiff = 1000

    def test_fail(self):
        P = GLPSParser()
        self.assertRaisesRegexp(RuntimeError, ".*invalid charactor.*",
                                P.parse, b"\xff\xfe")

        self.assertRaisesRegexp(RuntimeError, "No beamlines defined by this file",
                                P.parse, "")

        self.assertRaisesRegexp(RuntimeError, "No beamlines defined by this file",
                                P.parse, "A = 3;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "A = =;")

        self.assertRaisesRegexp(RuntimeError, ".*referenced before definition",
                                P.parse, "A = A;")

        self.assertRaisesRegexp(RuntimeError, ".*referenced before definition",
                                P.parse, "A = B;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "A = [;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "A = [ 1, 2;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "A = [ 1, 2, ;")

        self.assertRaisesRegexp(RuntimeError, ".*Vector element types must be scalar not type.*",
                                P.parse, "A = [ 1, \"bar\", ;")

        self.assertRaisesRegexp(RuntimeError, ".*Unterminated quote",
                                P.parse, "A = \"oops ...")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "A: line = (")

        self.assertRaisesRegexp(RuntimeError, ".*invalid.*referenced before definition",
                                P.parse, "A: line = ( invalid")

        self.assertRaisesRegexp(RuntimeError, ".*invalid.*referenced before definition",
                                P.parse, "bar: foo; A: line = ( bar, invalid")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "bar:: foo;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, "bar: : foo;")

        self.assertRaisesRegexp(RuntimeError, "syntax error",
                                P.parse, ":bar : foo;")
    def test_calc_err(self):
        P = GLPSParser()
        self.assertRaisesRegexp(RuntimeError, ".*division results in non-finite value",
                                P.parse, "foo = 4/0;")
    def test_utf8(self):
        "Can pass any 1-255 in quoted string"
        P = GLPSParser()

        C = P.parse("""
hello = "test\x1f";
x1: drift, L=4; # comments are ignored
foo: LINE = (x1, x1);
""")
        
        self.assertListEqual(C, [
            ('elements', [
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',4.0),('name','x1'), ('type','drift')],
            ]),
            ('hello', "test\x1f"),
            ('name', 'foo'),
        ])

    def test_good(self):
        P = GLPSParser()

        C = P.parse("""
hello = 42;
x1: drift, L=4;
foo: LINE = (x1, x1);
""")

        self.assertListEqual(C, [
            ('elements', [
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',4.0),('name','x1'), ('type','drift')],
            ]),
            ('hello', 42.0),
            ('name', 'foo'),
        ])

    def test_good2(self):
        P = GLPSParser()

        C = P.parse("""
hello = 42;
x1: drift, L=4;
x:2: quad, L=1;
f:oo: LINE = (2*x1, x:2);
""")

        self.assertListEqual(C, [
            ('elements', [
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',1.0),('name','x:2'), ('type','quad')],
            ]),
            ('hello', 42.0),
            ('name', 'f:oo'),
        ])

    def test_good3(self):
        P = GLPSParser()

        C = P.parse("""
hello = 42;
S: source;
x1: drift, L=4;
x2: quad, L=1;
foo: LINE = (S, 2*x1, x2);
""")

        E = [
            ('elements', [
                [('name','S'), ('type','source')],
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',1.0),('name','x2'), ('type','quad')],
            ]),
            ('hello', 42.0),
            ('name', 'foo'),
        ]
        try:
            self.assertListEqual(C, E)
        except:
            from pprint import pformat
            print('Actual', pformat(C))
            print('Expect', pformat(E))
            raise

    def test_calc(self):
        P = GLPSParser()

        C = P.parse("""
hello = 4*10--2;
x1: drift, L=4;
foo: LINE = (0*x1, (3-1)*x1);
""")

        self.assertListEqual(C, [
            ('elements', [
                [('L',4.0),('name','x1'), ('type','drift')],
                [('L',4.0),('name','x1'), ('type','drift')],
            ]),
            ('hello', 42.0),
            ('name', 'foo'),
        ])

    def test_arr(self):
        P = GLPSParser()
        C = P.parse("""
hello = [1,2, 3, 4];
x1: drift, L=4, extra = [1, 3, 5];
foo: LINE = (x1, x1);
""")
        C = OrderedDict(C)

        assert_array_equal(C['hello'], asarray([1,2,3,4]))
        C = OrderedDict(C['elements'][0])
        assert_array_equal(C['extra'], asarray([1,3,5]))

class testScopeDict(unittest.TestCase):
    'Test Config scoping when constructing from dict'

    def test_right(self):
        L = OrderedDict([
            ('phi', 1.0), # define before use
            ('elements', [
                [('name', 'X'), ('type', 'sbend'),],
            ]),
            ('sim_type', 'Vector'),
        ])

        M = Machine(L)
        self.assertEqual(M.conf(0)['phi'], 1.0)

    def test_wrong(self):
        L = OrderedDict([
            ('elements', [
                [('name', 'X'), ('type', 'sbend'),],
            ]),
            ('phi', 1.0), # definition after use (implied under 'elements')
            ('sim_type', 'Vector'),
        ])

        self.assertRaisesRegexp(KeyError, '.*issing.*phi.*', Machine, L)

    def test_overwrite1(self):
        L = OrderedDict([
            ('phi', 1.0),
            ('elements', [
                [('name', 'X'), ('type', 'sbend'), ('phi',2.0), ],
            ]),
            ('sim_type', 'Vector'),
        ])

        M = Machine(L)
        self.assertEqual(M.conf(0)['phi'], 2.0)

    def test_overwrite2(self):
        L = OrderedDict([
            ('elements', [
                [('name', 'X'), ('type', 'sbend'), ('phi',2.0), ],
            ]),
            ('phi', 1.0),
            ('sim_type', 'Vector'),
        ])

        M = Machine(L)
        self.assertEqual(M.conf(0)['phi'], 2.0)

@unittest.skip("Disable HDF5")
class testHDF5(unittest.TestCase):
    def test_good_explicit(self):
        P = GLPSParser()

        with open(os.path.join(datadir, "test_h5.lat"), "rb") as F:
            C = P.parse(F.read(), path=datadir) # explicitly provide path
        C = OrderedDict(C)
        self.assertEqual(C['plainname'], os.path.join(datadir, "test.h5"))
        self.assertEqual(C['h5name'], os.path.join(datadir, "test.h5/foo/baz"))

    def test_good_implicit(self):
        P = GLPSParser()

        with open(os.path.join(datadir, "test_h5.lat"), "rb") as F:
            C = P.parse(F) # uses os.path.dirname(F.name)
        C = OrderedDict(C)

        self.assertEqual(C['plainname'], os.path.join(datadir, "test.h5"))
        self.assertEqual(C['h5name'], os.path.join(datadir, "test.h5/foo/baz"))

class testNest(unittest.TestCase):
    def test_parse(self):
        """The parse() function evaluates to a recursive Config
        """
        P = GLPSParser()

        C = P.parse("""
x1: drift, L=4, nest = parse("%s");
foo: LINE = (x1);
"""%os.path.join(datadir,"parse1.lat"))
        print("actual", C)

        self.assertEqual(C, [
            ('elements', [
                [
                    ('L', 4.0),
                    ('name', 'x1'),
                    ('nest', [
                        [
                            ('elements', [
                                [('name','foo'), ('type','bar')],
                                [('name','foo'), ('type','bar')],
                            ]),
                            ('name', 'baz'),
                        ],
                    ]),
                    ('type', 'drift'),
                ],
            ]),
            ('name', 'foo'),
        ])

class testLog(unittest.TestCase):
    class CaptureHandler(logging.Handler):
        def __init__(self, *args, **kws):
            super(testLog.CaptureHandler, self).__init__(*args, **kws)
            self._L = []
        def emit(self, record):
            self._L.append(record)

    def test_name(self):
        self.assertEqual(getLoggerName(), "flame.machine")
        self.assertEqual(FLAME_ERROR, logging.ERROR)
        self.assertEqual(FLAME_WARN, logging.WARN)
        self.assertEqual(FLAME_INFO, logging.INFO)
        self.assertEqual(FLAME_DEBUG, logging.DEBUG)

    def test_capture_default(self):
        L = logging.getLogger(getLoggerName())
        H = self.CaptureHandler()
        L.addHandler(H)

        L.setLevel(logging.DEBUG)

        lattice = OrderedDict([
            ('elements', [
                [('name', 'X'), ('type', 'sbend'), ('phi', 1.0),],
            ]),
            ('sim_type', 'Vector'),
        ])

        M = Machine(lattice)

        # by default FLAME_DEBUG is disabled, so only one log record from Machine ctor
        self.assertRegexpMatches(H._L[0].msg, "Constructing Machine")
        self.assertRegexpMatches(H._L[0].filename, "base.cpp$")
        self.assertGreater(H._L[0].lineno, 1)
        self.assertEqual(H._L[0].levelno, logging.INFO)

        self.assertEqual(len(H._L), 1)

        L.removeHandler(H)

    def test_capture_default(self):
        L = logging.getLogger(getLoggerName())
        H = self.CaptureHandler()
        L.addHandler(H)

        L.setLevel(logging.DEBUG)
        setLogLevel(FLAME_DEBUG)

        lattice = OrderedDict([
            ('elements', [
                [('name', 'X'), ('type', 'sbend'), ('phi', 1.0),],
            ]),
            ('sim_type', 'Vector'),
        ])

        M = Machine(lattice)

        self.assertRegexpMatches(H._L[0].msg, "Constructing Machine")
        self.assertRegexpMatches(H._L[0].filename, "base.cpp$")
        self.assertGreater(H._L[0].lineno, logging.ERROR) # check that lineno and levelno aren't mixed up.  assume __LINE__>100
        self.assertEqual(H._L[0].levelno, logging.INFO)

        self.assertRegexpMatches(H._L[1].msg, "Complete constructing Machine")
        self.assertRegexpMatches(H._L[1].filename, "base.cpp$")
        self.assertGreater(H._L[1].lineno, H._L[0].lineno)
        self.assertEqual(H._L[1].levelno, logging.DEBUG)

        self.assertEqual(len(H._L), 2)

        L.removeHandler(H)

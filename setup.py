import os
import sys
from setuptools import setup, find_packages, Extension



if 'EPICS_BASE' not in os.environ or 'EPICS_HOST_ARCH' not in os.environ:
    print(sys.stderr, 'EPICS_BASE and EPICS_HOST_ARCH must be set')
    sys.exit(-1)

if sys.platform == 'darwin':
    libsrc = 'Darwin'
    compiler = 'clang'
elif sys.platform.startswith('linux'):
    libsrc = 'Linux'
    compiler = 'gcc'

epics_inc = os.path.join(os.environ['EPICS_BASE'], 'include')
epics_lib = os.path.join(os.environ['EPICS_BASE'], 'lib', os.getenv('EPICS_HOST_ARCH'))

compiler_args = ['-Wall', '-std=c++14']



ca_extension = Extension('ca_client.ca',
    language = 'c++',
    sources = list(map(lambda s: os.path.join('src/ca_client/ca', s), [
        'ca.cpp'
    ])),
    include_dirs = [
        'src/ca_client/ca',
        epics_inc,
        os.path.join(epics_inc, 'os', libsrc),
        os.path.join(epics_inc, 'compiler', compiler),
    ],
    library_dirs = [ epics_lib ],
    runtime_library_dirs = [ epics_lib ],
    extra_compile_args = compiler_args,
    libraries = ['ca']
)

cac_extension = Extension('ca_client.cac',
    language = 'c++',
    sources = list(map(lambda s: os.path.join('src/ca_client/cac', s), [
        'cac.cpp',
        'pv.cpp',
        'convert.cpp'
    ])),
    include_dirs = [
        'src/ca_client/cac',
        epics_inc,
        os.path.join(epics_inc, 'os', libsrc),
        os.path.join(epics_inc, 'compiler', compiler),
    ],
    library_dirs = [ epics_lib ],
    runtime_library_dirs = [ epics_lib ],
    extra_compile_args = compiler_args,
    libraries=['Com', 'ca']
)


setup(
    name = 'ca_client',
    description = 'Channel Access client library',
    long_description = '',
    license='MIT',
    author = 'André Althaus',
    author_email = 'andre.althaus@tu-dortmund.de',
    classifiers = [
        'Development Status :: 3 - Alpha',
        'License :: OSI Approved :: MIT License',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.4',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Topic :: Scientific/Engineering'
    ],
    keywords = 'epics ca',
    packages = find_packages('src'),
    package_dir = { '': 'src' },
    ext_modules = [ ca_extension, cac_extension ],
    python_requires = '>= 3.4',
    setup_requires = [ 'setuptools_scm' ],
    install_requires = [],
    extras_require = {
        'dev': [ 'tox', 'sphinx', 'pytest' ],
        'doc': [ 'sphinx' ],
        'test': [ 'pytest' ]
    },
    use_scm_version = True
)

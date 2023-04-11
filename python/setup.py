from setuptools import Extension, setup
from Cython.Build import cythonize

ext = Extension(
    name = 'cpddl',
    sources = ['cpddl.pyx'],
    libraries = ['pddl'],
    library_dirs = ['../'],
    include_dirs = ['../'],
)
setup(
    name = 'cpddl',
    ext_modules = cythonize(ext)
)

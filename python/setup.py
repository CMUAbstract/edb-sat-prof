from setuptools import setup, find_packages

setup(
    name='edbsat',
    version='0.0.1',
    description='',
    url='https://github.com/CMUAbstract/releases/blob/master/EDBsat.md',
    author='Alexei Colin',
    author_email='ac@alexeicolin.com',
    license='Unlicense',
    packages=['edbsat'],
    python_requires='>=3',

    install_requires=[
        'argparse',
        'pycrc',
        'odroidshow',
    ],

    entry_points={
        'console_scripts': [
            'edbsat-decode=edbsat.decode',
        ],
    },
)

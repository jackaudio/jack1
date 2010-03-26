#!/usr/bin/env python

from distutils.core import setup

setup(name='pyjacksm',
	version='0.1',
	description='jack session manager',
	author='Torben Hohn',
	author_email='torbenh@gmx.de',
	url='http://www.jackaudio.org/',
	packages=['pyjacksm'],
	package_dir={'pyjacksm': 'src/pyjacksm'},
	scripts=['src/sessionmanager.py'],
	data_files=[('/usr/share/dbus-1/services', ['data/org.jackaudio.sessionmanager.service'])]
	)


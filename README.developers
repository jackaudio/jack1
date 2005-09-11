=======================================================================
*** README.developers - JACK development practices                  ***
=======================================================================

:Version: $Id$
:Formatting: restructured text, http://docutils.sourceforge.net/rst.html

What is this? 
-----------------------------------------------------------------------

This file is a collection of practices and rules for JACK
development. If you have questions, or would like to make 
changes, raise the issue on jackit-devel (see 
http://lists.sourceforge.net/lists/listinfo/jackit-devel ).


Contents
-----------------------------------------------------------------------

- What is this?
- Version numbers
- Important files for developers
- Sending patches
- CVS Access
- Decision Process


Important files for developers
-----------------------------------------------------------------------

AUTHORS
	List of contributors. If you have contributed code, mail Paul 
	Davis to get your name added to the list, or if you have 
	CVS-access, help yourself. :) Also remember to update the
	per source file copyright statements when committing changes.

README.developers 
	This file.

TODO
	A one file mini-bugzilla for JACK developers. Note: this file
	is no longer actively updated - please use the Mantis
	bugtracker instead, see http://jackit.sourceforge.net/dev/ .

libjack/ChangeLog
	A list of _all_ changes to the public interface!


Version numbers 
-----------------------------------------------------------------------

JACK's package version
~~~~~~~~~~~~~~~~~~~~~~

JACK's package version is set in configure.in, and consists of 
major, minor and revision numbers. This version should be 
updated whenever a non-trivial set of changes is committed 
to CVS:
 
major version
   ask on jackit-devel :)

minor version
   incremented when any of the public or internal
   interfaces are changed

revision
   incremented when implementation-only
   changes are made

Client API versioning
~~~~~~~~~~~~~~~~~~~~~

JACK clients are affected by two interfaces, the JACK Client API (libjack)
and the JACK Client Protocol API (interface between jackd and 
libjack). The former one is versioned using libtool interface 
versioniong (set in configure.in). This version should be updated 
whenever a set of changes affecting the interface is committed 
to CVS:

current
    incremented whenever the public libjack API is changed 
   
revision
    incremented when the libjack implementation is changed
    
age
    current libjack is both source and binary compatible with
    libjack interfaces current,current-1,...,current-age

Note! It was decided in January 2003 that current interface number
      will remain as zero until the first stable JACK version
      is released.

JACK Client Protocol is versioned... <TBD>.

Note! All changes that affect the libjack API must be documented 
in jack/libjack/ChangeLog using the standard ChangeLog style
(see GNU developer docs).


Sending patches
---------------------------------------------------------------------

People without CVS-access
~~~~~~~~~~~~~~~~~~~~~~~~~

Send your patches to jackit-devel. Normally patches are accepted
by Paul Davis and/or Jack O'Quin. Either the patch is applied right 
away, or you get an "ok to me" message, after which other developers 
with CVS-access can commit the patch.

People with CVS-access
~~~~~~~~~~~~~~~~~~~~~~

Trivial changes can be committed without review. For non-trivial 
changes, you should first send a proposal to jackit-devel and
wait for comments. There are no strict approval rules so use of
common sense is recommended. ;)

Tips for making patches
~~~~~~~~~~~~~~~~~~~~~~~

- test your patch on a clean CVS-checkout
- remember to run cvs update before making commits


CVS Access
-----------------------------------------------------------------------

Contact Paul Davis.


Uses of external libraries and other packages
-----------------------------------------------------------------------

The main JACK components, jackd and libjack, should only use 
standard POSIX and ANSI-C services. If use of other interfaces is
absolutely needed, it should be made optional in the build process (via
a configure switch for example). 

Other components like example clients and drivers, may rely on other 
packages, but these dependencies should not affect the main build 
process.


Decision Process
-----------------------------------------------------------------------

All non-trivial changes should be discussed on the jackit-devel 
mailing list. It has been agreed that Paul Davis will make 
the final decisions based on these discussions.


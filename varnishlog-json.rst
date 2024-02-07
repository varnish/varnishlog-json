..
	Copyright (c) 2024 Varnish Software AS
	SPDX-License-Identifier: BSD-2-Clause
	See LICENSE file for full text of license

.. role:: ref(emphasis)

.. _varnishlog-json(1):

===============
varnishlog-json
===============

----------------------------
Display Varnish logs in JSON
----------------------------

:Manual section: 1

SYNOPSIS
========

.. include:: varnishlog-json_synopsis.rst
varnishlog-json |synopsis|

OPTIONS
=======

The following options are available:

.. include:: varnishlog-json_options.rst

SIGNALS
=======

* SIGHUP

  Rotate the log file (see -w option) in daemon mode,
  abort the loop and die gracefully when running in the foreground.

* SIGUSR1

  Flush any outstanding transactions

SEE ALSO
========
* :ref:`varnishd(1)`
* :ref:`vsl(7)`
* :ref:`vsl-query(7)`
* :ref:`varnishlog(7)`
* :ref:`varnishncsa(7)`

COPYRIGHT
=========

This document is licensed under the same licence as Varnish
itself. See LICENCE for details.

* Copyright (c) 2024 Varnish Software AS

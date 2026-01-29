.. Software TSN Switch documentation master file, created by
   sphinx-quickstart on Mon Jan 26 11:48:02 2026.
   You can adapt this file completely to your liking, but it should at least
   contain the root `toctree` directive.

Software TSN Switch user documentation
======================================

With our software TSN switch, you can connect Docker containers and virtual machines on a Debian device to your TSN network.
This project also includes a Centralized Network Control (CNC) daemon for Debian, to configure and control both our and other switches in your TSN network from a single, centralized device.

.. toctree::
   :maxdepth: 2
   :caption: Getting started

   gettingStarted/overview
   gettingStarted/demo

.. toctree::
   :maxdepth: 2
   :caption: The software switch

   Installation <tsnctrld/install>
   Quickstart <tsnctrld/quickstart>
   CLI <tsnctrld/cli>

.. toctree::
   :maxdepth: 2
   :caption: The CNC

   Installation <cnc/install>
   Quickstart <cnc/quickstart>
   CLI <cnc/cli>

.. toctree::
   :maxdepth: 1
   :caption: Developers

   Dev documentation <http://enpro-switch-64df46.gitlab-pages-vs.informatik.uni-stuttgart.de/docs-dev>

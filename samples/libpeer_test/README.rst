.. zephyr:code-sample:: libpeer_test
   :name: libpeer WebRTC datachannel test

   Sample application for libpeer. Establishes a datachannel WebRTC
   connection using the https://sepfy.github.io service.

Overview
********

This sample demonstrates the use of libpeer by establishing a
datachannel WebRTC connection to the https://sepfy.github.io test
service. This is meant as a test for the libpeer Zephyr integration.

Building and Running
********************

Build the sample application like this:

.. zephyr-app-commands::
   :zephyr-app: samples/libpeer_test
   :board: <board to use>
   :goals: build

Example building for :zephyr:board:`native_sim`:

.. zephyr-app-commands::
   :zephyr-app: samples/libpeer_test
   :board: native_sim
   :goals: build

Example building for Raspberry Pi Pico 2W:

.. zephyr-app-commands::
   :zephyr-app: samples/libpeer_test
   :board: rpi_pico2/rp2350a/m33_0/w
   :goals: build flash

Once flashed, connect a serial terminal to the board and use the Zephyr
shell to start the test with the `libpeer_test` shell command and
follow the instructions to start the peer in a browser:

.. code-block:: console
   libpeer_test <connection_id>
   [00:04:54.240,000] <inf> libpeer_test: Open https://sepfy.github.io/libpeer/?id=<connection_id> in a browser and click 'Connect'

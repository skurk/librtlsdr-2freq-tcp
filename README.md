# librtlsdr for 2-Frequency-Reception with built-in TCP server for remote triggering captures

## Description
Modified librtlsdr (*rtl_sdr* command) to enable seamless switching of frequency during reception.
This is based on the async_rearrangement branch of librtlsdr.

The librtlsdr-2freq-tcp mod includes a simple TCP server which, by default, listens to TCP connections on port 4500 by default. Change port number with the command line parameter -P.
To trigger a recording, a remote client sends tdoa:<id>, for example tdoa:12345, which starts the capture using the preset parameters and saves the output to <id>.dat, or 12345.dat using the example.

This lib enables TDOA localization with RTL-SDRs, when a reference transmitter is used for synchronization. More information:
<http://www.panoradio-sdr.de/tdoa-transmitter-localization-with-rtl-sdrs/>


## Usage
*rtl_sdr -f <frequency 1> -h <frequency 2> -n <num_samples> [-P <tcp_port>]

Awaits TCP connection and command to trigger recording. Then, it receives the first <num_samples> IQ samples at <frequency 1> (in Hz), followed by <num_samples> IQ samples at <frequency 2>, and finally <num_samples> at <frequency 1> without interruption. For help and more options type *rtl_sdr*.

Example Usage:
*rtl_sdr -f 200e6 -h 100e6 -n 1e3*
receives first 1000 IQ samples at 200 MHz, then 1000 IQ samples at 100 MHz, then again 1000 at 200 MHz without interruption, resulting in 3000 samples in total.


## Build Instructions
1. navigate to main folder librtlsdr-2freq/
2. *mkdir build*
3. *cd build*
4. *cmake ../*
5. *make*

binaries (rtl_sdr) can then be found in build/src/

Based on the work of DC9ST, 2017-2019
TCP mod by LB5SH, 2023

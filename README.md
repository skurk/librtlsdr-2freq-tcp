# librtlsdr for 2-Frequency-Reception with built-in TCP server for remote triggering captures

## Description
Modified librtlsdr (*rtl_sdr* command) to enable seamless switching of frequency during reception.
This is based on the async_rearrangement branch of librtlsdr.

This lib enables TDOA localization with RTL-SDRs, when a reference transmitter is used for synchronization. More information:
<http://www.panoradio-sdr.de/tdoa-transmitter-localization-with-rtl-sdrs/>

## Purpose of this fork
This was written while chasing down a major source of QRM to our local 2m FM repeater. The main purpose is having a remote centralized server triggering captures on all nodes simultaneously. At this point the server already knows the duration of the recording, and can automatically collect the recorded files for further processing.

The fork introduces a simple TCP server to the existing librtlsdr-2freq project, which by default listens for connection to TCP port 4500. Since all our nodes were isolated on a wireguard NAT, security was not a priority and hence no authentication is requried to connect to each client.

## Remote trigger a recording
To trigger a recording, a remote server connects to the client and sends the string "tdoa:<id>", for example "tdoa:12345". The recording is immediately started, and the output file will be named <id>.dat, for example 12345.dat. For our case scenario we had the server trigger recording on all nodes, wait for a known period, then collected all files using `scp` for processing with a custom made TDoA analyzer.

When the recording is completed, the program continues listening for new connections, meaning this could monitor the frequencies indefinitely.

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

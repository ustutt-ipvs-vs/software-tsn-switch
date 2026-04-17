# Software switch command line arguments

All command line arguments for `tsnctld` relate to clock synchronization.
We recommend you to read the  [page about clock synchronization](clock_sync.md) first, it contains an introduction to synchronization strategies, as well as example commands.
This page focuses more on the technical details and is intended for those who are already familiar with gPTP.

Under the hood, `tsnctrld` achieves clock synchronization by starting and monitoring executables from the [chrony](https://chrony-project.org/documentation.html) and [linuxptp](https://linuxptp.nwtime.org/documentation/) packages.

### `-d / --disable-clock-sync`
Skips clock synchronization altogether.
Use this flag on all instances of `tsnctrld` if you want to set up your own clock synchronization.
Not providing this flag would cause `tsnctrld`'s clock synchronization to interfere with your own, likely causing time skips.
When you use this flag it is then your responsibility that `CLOCK_REALTIME` and `CLOCK_TAI` are synchronized across all devices that run `tsnctrld`.

Due to this option disabling clock synchronization, it is incompatible with other options that modify clock synchronization, namely `-i / --nic`, `-g / --grandmanster`, and `-n / --ntp`

### `-n / --nic`

When you run `tsnctrld`, you must either provide this option or `-d / --disable-clock-sync`.
When you provide this option, you must follow it up with one or more network interface card (NIC) names, the NICs you want to run gPTP on.

`tsnctrld` launches `ptp4l` to synchronize the NIC's PHCs with other devices and launches `phc2sys` to synchronize the PHCs with each other and `CLOCK_REALTIME` (and therefore `CLOCK_TAI`).
`CLOCK_TAI` is hardcoded to be 37 seconds ahead of `CLOCK_REALTIME`, there is no automated way to adjust this offset in case of a leap second.
`ptp4l` uses the default [gPTP.cfg](https://linuxptp.nwtime.org/documentation/configs/gptp/) configuration.

### `-g / --grandmaster`

Configures this device's clocks to have a PTP `priority1` value of 10 instead of the default of 248, which makes this device's clocks a lot more likely to be elected grandmaster.

If you didn't also provide the `-g / --grandmaster` flag, `tsnctld` will run `chronyd` (an NTP daemon) in one-time sync mode to synchronize the hadware clocks and software clocks on this device with the world clocks a single time at startup.

### `-n / --ntp`

When you provide this flag you must also provide the `-g / --grandmaster` flag.
With this flag provided, instead of running `chronyd` in one-time sync mode, it is being run continously, disciplining `CLOCK_REALTIME` using NTP with a very light slew of max. 694ppm (about 1 minute per 24 hours).
Also, instead of the PHCs disciplining `CLOCK_REALTIME`, `CLOCK_REALTIME` now disciplines the PHCs.
This is why it's important that the device running `tsnctrld` this mode becomes the gPTP grandmaster, since its PHCs will not respect the timestamps of any other device's clock.

### `-m / --monitor-external-ptp`

Can only be used when using the `--disable-clock-sync` flag.
This is useful if you want to set up your own clock synchronization with `ptp4l`, but still want to let `tsnctrld`
collect the performance monitoring metrics into the NETCONF datastore.

## Further reading

Read the [page about clock synchronization](clock_sync.md) to learn more about the general concepts of how these command line arguments are supposed to be used.

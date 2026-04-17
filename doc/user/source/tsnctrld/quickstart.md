# Software switch quickstart

Of all your device in your TSN network, one device should be the grandmaster, ideally the one with the most precise hardware clock(s).
If you want a `tsnctrld` device to be that grandmaster:

```
sudo tsnctrld --grandmaster --ntp --nic <NIC> [<NIC> ...]
```

Otherwise, use

```
sudo tsnctrld --nic <NIC> [<NIC> ...]
```

Check the page about [clock synchronization](clock_sync.md) for more information. 
While the daemon runs, you can make configurations via NETCONF (for example the CNC)!

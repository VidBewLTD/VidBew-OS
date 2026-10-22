Small helpers for testing VidBew/OS headless in QEMU.

qtest.py     boots an image with no display, lets a script press keys, move/click the
             mouse, take screenshots (PPM -> PNG) and read the serial log.
               python3 qtest.py os.img shot.png 3        # boot, wait 3 s, screenshot + serial log
             or import it:  from qtest import VM
fakedns.py   answers every DNS query with 93.184.216.34 on 127.0.0.1:53. QEMU's user
             network forwards guest traffic for 10.0.2.2 to the host, so in the guest:
               anchor 10.0.2.15 255.255.255.0 10.0.2.2 10.0.2.2
               seek example.com
setmode.py   writes a display mode into an image's settings sector, e.g.
               python3 setmode.py os.img test.img 1024 768 32

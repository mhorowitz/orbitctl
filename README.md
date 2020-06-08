orbitctl
========
This is a tool to manipulate the proprietary extensions of the Logitech QuickCam Orbit AF on the Macintosh.

It's not fancy.  It can only deal with one device connected.  Diagnostics are not very good.  If you want to see what's happening in the camera, run a separate tool, like Photo Booth.

For setting the standardized features, https://github.com/jtfrey/uvc-util seems to work pretty well.

running
=======
```
$ orbitctl
usage: orbitctl cmd [opts ...]
  scan
  reset
  pan left | right
  tilt up | down
  led on | off | auto
```

building
========
```
cd src
make
```

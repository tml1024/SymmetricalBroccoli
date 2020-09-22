# SymmetricalBroccoli

This is a plug-in to X-Plane 11 that receives packets over UDP from a
mobile phone app or other source in the simple format that is used by
many other head tracking applications.

Each packet is 48 bytes and consists of six doubles (8-byte floating
point numbers): The user head's x, y, z position (in centimetres)
relative to the phone, and yaw, pitch, and roll angle (in degrees).

One source for such packets is the iOS app [Head
Tracker](https://apps.apple.com/us/app/head-tracker/id1527710071).
That is what I use. It is simple and works.

The name is one of those that GitHub automatically suggests. Why not?

Future plans
------------

This is a work in progress.

* The filtering of the input needs work.
* It would be nice to make it work for MSFS, too, or maybe MSFS
  already by itself can accept such UDP packets?

General musings
---------------

I wonder why X-Plane itself doesn't have the option to listen to this
kind of UDP packets? As seen from the source code here, it is quite
trivial. (More than half of the source code here is not directly
related to the actual task but is for logging, or portability to the
three platforms.)

Why didn't I just use OpenTrack instead? I love to code, and it was
more fun to start from scratch to make something minimal but
sufficient. After one evening I had something that basically worked. I
had a look at the OpenTrack [source
code](https://github.com/opentrack/opentrack.git) and it seemed quite
over-engineered for my simple needs.

Build instructions: macOS
-------------------------

Open the Xcode project file. In the project's Build Settings, change
the User-Defined setting XPSDK_ROOT to point to where you have the
X-Plane 3 SDK unpacked. Build. You will end up with
SymmetricalBroccoli.xpl in the project's top folder.

Copy SymmetricalBroccoli.xpl to the Resources/plugins folder your
X-Plane 11 installation.

Build instructions: Linux
-------------------------

Edit the Makefile and change the XPSDK and XP11 values, and other
things, as appropriate. Run _make install_.

Build instructions: Windows
---------------------------

Open the Visual Studio project file. Either you need to have the
X-Plane SDK unpacked as "SDK" in the project folder, or change the
include and library paths in the project properties. Build. Copy the
resulting SymmetricalBroccoli.xpl to the Resources\plugins folder your
X-Plane 11 installation.

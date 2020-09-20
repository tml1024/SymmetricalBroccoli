# The location of the 3.0 (or later X-Plane SDK)
XPSDK=$(HOME)/XPSDK

# The location of your X-Plane 11 installation
XP11=$(HOME)/XP11

# The name of this plug-in
MYNAME=SymmetricalBroccoli

# Whether to show debug info in a window, 0 or 1
DEBUGWINDOW=0

# Whether to produce copious log data into a file in /tmp
DEBUGLOGDATA=1

DEFINES=-DDEBUGWINDOW=$(DEBUGWINDOW) -DDEBUGLOGDATA=$(DEBUGLOGDATA)

CXX=clang++

# APL: macOS
# IBM: Windows
# LIN: Linux
CFLAGS=-std=c++17 -Werror -I$(XPSDK)/CHeaders/XPLM $(DEFINES) -DAPL=0 -DIBM=0 -DLIN=1 -DXPLM200 -DXPLM210 -DXPLM300 -DXPLM301 -DXPLM302 -DXPLM303 -Wall -fpic -Ofast

all : $(MYNAME).xpl

install : all
	cd $(XP11)/Resources/plugins && mkdir -p $(MYNAME)/lin_x64
	cp $(MYNAME).xpl $(XP11)/Resources/plugins/$(MYNAME)/lin_x64/$(MYNAME).xpl

clean :
	rm $(MYNAME).xpl

$(MYNAME).xpl : $(MYNAME).cpp
	$(CXX) $(CFLAGS) $(MYNAME).cpp -shared -o $(MYNAME).xpl

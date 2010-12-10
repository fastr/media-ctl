media-ctl
====

Used to configure V4L2 devices.

    git clone git://github.com/fastr/media-ctl.git
    cd media-ctl
    make

FSR172X
----

The FSR must be configured with media-ctl before it is ready for use.

    width=2048
    height=128
    size=${width}x${height}
    
    ./media-ctl -r -l '"fsr172x 3-0010":0->"OMAP3 ISP CCDC":0[1], "OMAP3 ISP CCDC":1->"OMAP3 ISP CCDC output":0[1]'
    ./media-ctl -f "\"fsr172x 3-0010\":0[SGRBG12 $size], \"OMAP3 ISP CCDC\":1[SGRBG12 $size]"
    ./media-ctl -e 'OMAP3 ISP CCDC output'

TODO
----

**Cross-compiling**

    #!/bin/sh -e
    export CSTOOLS=/opt/code-sourcery/arm-2009q1
    export CSTOOLS_INC=${CSTOOLS}/arm-none-linux-gnueabi/libc/usr/include
    export CSTOOLS_LIB=${CSTOOLS}/arm-none-linux-gnueabi/libc/usr/lib
    export LDFLAGS="-L${CSTOOLS_LIB} -Wl,-rpath-link,${CSTOOLS_LIB} -Wl,-O1 -Wl,--hash-style=gnu"
    export CXXFLAGS="-isystem${CSTOOLS_INC} -fexpensive-optimizations -frename-registers -fomit-frame-pointer -O2 -ggdb3 -fpermissive -fvisibility-inlines-hidden"

    export ARCH=arm
    export CROSS_COMPILE=${HOME}/overo-oe/tmp/sysroots/i686-linux/usr/armv7a/bin/arm-angstrom-linux-gnueabi-

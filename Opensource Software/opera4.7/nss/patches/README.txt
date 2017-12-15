The following patches are needed to cross compile nss-3.19:

* arch.mk.patch
Allow overriding OS_TEST and OS_ARCH on the commandline, needed
for cross compilation magic to find out for what target nss should
be built. (Without this patch uname -r and uname -s is used to figure it out)

* Linux.mk.patch
 - Prepend gcc and fridns with ${CROSS} allowing for easy cross compilation.
 - Make it possible to not use system zlib by providing SYSTEM_ZLIB=0 env variable
 - Explictly tell zlib that unistd.h is available by HAVE_UNISTD_H=1 env variable

* nsspem.patch
Adds a new library, libnsspem.so, used by cURL to allow reading OpenSSL .pem
certificate files.  This patch is a snapshot of this commit:
https://git.fedorahosted.org/cgit/nss-pem.git/commit/?id=015ae754dd9f6fbcd7e52030ec9732eb27fc06a8

* shlibsign.patch
The shlibsign tool is built for the target so allow for skipping the signing
step by SKIP_SHLIBSIGN=1 env variable.

Build with this command line for mipsel:
NSPR_CONFIGURE_OPTS="--target=mipsel-linux" NATIVE_CC=gcc OS_ARCH=Linux OS_RELEASE=2.6 OS_TARGET=Linux OS_TEST=mips BUILD_OPT=1 NSDISTMODE=copy make nss_build_all

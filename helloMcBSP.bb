DESCRIPTION = "hello kernel world"
PRIORITY = "optional"
SECTION = "base"
LICENSE = "GPL"
RDEPENDS = "kernel (${KERNEL_VERSION})"
DEPENDS = "virtual/kernel"
#based on char-driver

PR = "r0"

SRC_URI = "\
file://hellokernel.c \
file://Makefile\
"

S = "${WORKDIR}/hellokernel"

inherit module-base

addtask builddir after do_fetch before do_unpack
addtask movesrc after do_unpack before do_patch

EXTRA_OEMAKE = 'CROSS_COMPILE="${CROSS_COMPILE}" \
                KERNELDIR="${KERNEL_SOURCE}" \
                CC="${CC}" \
                '

PARALLEL_MAKE = ""

do_builddir () {
   mkdir -p ${S}
}

do_movesrc () {
   cd ${WORKDIR}
   mv hellokernel.c Makefile ${S}
}

do_configure () {
        echo "Nothing to configure for hellokernel"
}

do_compile () {
   unset CFLAGS CPPFLAGS CXXFLAGS LDFLAGS
   cd ${S}  
        oe_runmake
}

do_install () {
   # kernel module installs with other modules
   install -m 0755 -d ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
   # use cp instead of install so the driver doesn't get stripped
   cp ${S}/hellokernel.ko \
   ${D}${base_libdir}/modules/${KERNEL_VERSION}/extra/
}

PACKAGES = "${PN}"
FILES_${PN} += "${base_libdir}/modules/${KERNEL_VERSION}/extra/hellokernel.ko" 

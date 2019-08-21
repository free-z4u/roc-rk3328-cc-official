#! /usr/bin/env python
# -*- coding: utf-8 -*-

# Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in the
#       documentation and/or other materials provided with the distribution.
#     * Neither the name of The Linux Foundation nor
#       the names of its contributors may be used to endorse or promote
#       products derived from this software without specific prior written
#       permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
# OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
# WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Invoke gcc, looking for warnings, and causing a failure if there are
# non-whitelisted warnings.

import errno
import re
import os
import sys
import subprocess

# Note that gcc uses unicode, which may depend on the locale.  TODO:
# force LANG to be set to en_US.UTF-8 to get consistent warnings.

allowed_warnings = set([
    "vdso.c:128", #  warning: ‘memcmp’ reading 4 bytes from a region of size 1 [-Wstringop-overflow=]
    "regcache-rbtree.c:36", # warning: alignment 1 of ‘struct regcache_rbtree_node’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:350", # warning: alignment 4 of ‘struct sctp_paddr_change’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:670", # warning: alignment 4 of ‘struct sctp_setpeerprim’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:669", # warning: ‘sspp_addr’ offset 4 in ‘struct sctp_setpeerprim’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:683", # warning: alignment 4 of ‘struct sctp_prim’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:682", # warning: ‘ssp_addr’ offset 4 in ‘struct sctp_prim’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:730", # warning: alignment 4 of ‘struct sctp_paddrparams’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:724", # warning: ‘spp_address’ offset 4 in ‘struct sctp_paddrparams’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "sctp.h:843", # warning: alignment 4 of ‘struct sctp_paddrinfo’ is less than 8 [-Wpacked-not-aligned]
    "sctp.h:837", # warning: ‘spinfo_address’ offset 4 in ‘struct sctp_paddrinfo’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.h:51", # warning: ‘compat_sys_mbind’ alias between functions of incompatible types ‘long int(compat_ulong_t,  compat_ulong_t,  compat_ulong_t,  compat_ulong_t *, compat_ulong_t,  compat_ulong_t)’ {aka ‘long int(unsigned int,  unsigned int,  unsigned int,  unsigned int *, unsigned int,  unsigned int)’} and ‘long int(long int,  long int,  long int,  long int,  long int,  long int)’ [-Wattribute-alias]
    "compat.c:539", # warning: alignment 4 of ‘struct compat_group_req’ is less than 8 [-Wpacked-not-aligned]
    "compat.c:537", # warning: ‘gr_group’ offset 4 in ‘struct compat_group_req’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:547", # warning: alignment 4 of ‘struct compat_group_source_req’ is less than 8 [-Wpacked-not-aligned]
    "compat.c:543", # warning: ‘gsr_group’ offset 4 in ‘struct compat_group_source_req’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:545", # warning: ‘gsr_source’ offset 132 in ‘struct compat_group_source_req’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "compat.c:557", # warning: alignment 4 of ‘struct compat_group_filter’ is less than 8 [-Wpacked-not-aligned]
    "compat.c:551", # warning: ‘gf_group’ offset 4 in ‘struct compat_group_filter’ isn’t aligned to 8 [-Wpacked-not-aligned]
    "exec.c:1223", # warning: argument to ‘sizeof’ in ‘strncpy’ call is the same expression as the source; did you mean to use the size of the destination? [-Wsizeof-pointer-memaccess]
    "printk.c:140", # warning: ‘strncpy’ output truncated before terminating nul copying 2 bytes from a string of the same length [-Wstringop-truncation]
    "printk.c:143", # warning: ‘strncpy’ output truncated before terminating nul copying 3 bytes from a string of the same length [-Wstringop-truncation]
    "lkdtm_bugs.c:89", # warning: ‘memset’ writing 64 bytes into a region of size 8 overflows the destination [-Wstringop-overflow=]
    "ip_tunnel.c:264", # warning: ‘strncat’ specified bound 2 equals source length [-Wstringop-overflow=]
    "cfg80211.c:4170", # warning: ‘strncpy’ output truncated before terminating nul copying 3 bytes from a string of the same length [-Wstringop-truncation]
    "virtio_net.c:382", # warning: ignoring return value of ‘skb_to_sgvec’, declared with attribute warn_unused_result [-Wunused-result]
    "virtio_net.c:796", # warning: ignoring return value of ‘skb_to_sgvec’, declared with attribute warn_unused_result [-Wunused-result]
    "syscalls.h:211", # warning: ‘sys_fadvise64_64’ alias between functions of incompatible types ‘long int(int,  loff_t,  loff_t,  int)’ {aka ‘long int(int,  long long int,  long long int,  int)’} and ‘long int(long int,  long long int,  long long int,  long int)’ [-Wattribute-alias]
    "secureboot.c:19", # warning: duplicate ‘const’ declaration specifier [-Wduplicate-decl-specifier]
    "secureboot.c:22", # warning: duplicate ‘const’ declaration specifier [-Wduplicate-decl-specifier]
    "rx.c:208", # warning: alignment 1 of ‘struct <anonymous>’ is less than 2 [-Wpacked-not-aligned]
    "kernel.h:771", # warning: comparison of distinct pointer types lacks a cast
])

# Capture the name of the object file, can find it.
ofile = None

warning_re = re.compile(r'''(.*/|)([^/]+\.[a-z]+:\d+):(\d+:)? warning:''')
def interpret_warning(line):
    """Decode the message from gcc.  The messages we care about have a filename, and a warning"""
    line = line.rstrip('\n')
    m = warning_re.match(line)
    if m and m.group(2) not in allowed_warnings:
        print "error, forbidden warning:", m.group(2)

        # If there is a warning, remove any object if it exists.
        if ofile:
            try:
                os.remove(ofile)
            except OSError:
                pass
        sys.exit(1)

def run_gcc():
    args = sys.argv[1:]
    # Look for -o
    try:
        i = args.index('-o')
        global ofile
        ofile = args[i+1]
    except (ValueError, IndexError):
        pass

    compiler = sys.argv[0]

    try:
        proc = subprocess.Popen(args, stderr=subprocess.PIPE)
        for line in proc.stderr:
            print line,
            interpret_warning(line)

        result = proc.wait()
    except OSError as e:
        result = e.errno
        if result == errno.ENOENT:
            print args[0] + ':',e.strerror
            print 'Is your PATH set correctly?'
        else:
            print ' '.join(args), str(e)

    return result

if __name__ == '__main__':
    status = run_gcc()
    sys.exit(status)

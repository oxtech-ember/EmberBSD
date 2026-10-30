#!/usr/bin/env perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# List upstream-generated sources needed by native base crypto and ssl.
use strict;
use warnings;
use lib '.';
use configdata;
my %seen;
for my $target (qw(libcrypto libssl providers/libdefault.a providers/libcommon.a apps/libapps.a providers/liblegacy.a providers/legacy test/libtestutil.a apps/openssl)) {
    for my $object (@{$unified_info{sources}{$target} // []},
                    @{$unified_info{shared_sources}{$target} // []}) {
        next if $object =~ /\.(?:a|ld)\z/;
        for my $source (@{$unified_info{sources}{$object} // []},
                        @{$unified_info{depends}{$object} // []}) {
            next unless exists $unified_info{generate}{$source};
            die "unsafe generated source $source\n" if $source =~ /[^A-Za-z0-9_.\/+@%=-]/ || $source =~ m{(?:^|/)\.\.(?:/|$)};
            $seen{$source} = 1;
        }
    }
}
print "$_\n" for sort keys %seen;

#!/usr/bin/env perl
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Emit BSD make source groups from an upstream OpenSSL configdata.pm.
use strict;
use warnings;
use Cwd qw(abs_path);
use File::Basename qw(basename dirname);
use File::Spec;
use lib '.';
use configdata;

my %groups = (crypto => 'libcrypto', ssl => 'libssl',
              default => 'providers/libdefault.a', common => 'providers/libcommon.a', 'ssl-shared' => 'libssl', 'crypto-shared' => 'libcrypto', apps => 'apps/libapps.a', legacy => 'providers/liblegacy.a', 'legacy-module' => 'providers/legacy', test => 'test/libtestutil.a', cli => 'apps/openssl');
my $group = shift @ARGV // die "usage: config-sources.pl crypto|crypto-shared|ssl|ssl-shared|default|common|apps|legacy|legacy-module|test|cli\n";
die "unknown source group\n" unless exists $groups{$group} && !@ARGV;
my $target = $groups{$group};
my $objects = ($group =~ /-shared\z/ ? $unified_info{shared_sources}{$target} : $unified_info{sources}{$target}) // die "missing configured target $target\n";
# The three groups are compiled into the same OS libcrypto object directory.
# Detect cross-group collisions before emitting any one of them.
if ($group eq 'crypto' || $group eq 'crypto-shared' || $group eq 'default' || $group eq 'common') {
    my %combined;
    for my $part (qw(libcrypto providers/libdefault.a providers/libcommon.a)) {
        for my $object (@{$unified_info{sources}{$part} // die "missing configured target $part\n"}) {
            next if $object =~ /\.a\z/;
            my $inputs = $unified_info{sources}{$object} // die "missing source for $object\n";
            die "ambiguous object source for $object\n" unless @$inputs == 1;
            my $name = lc basename($inputs->[0]);
            $name =~ s/\.(?:c|s)\z//;
            die "duplicate libcrypto object stem $name\n" if $combined{$name}++;
        }
    }
}
my (%static_sources, %static_objects);
if ($group =~ /-shared\z/) {
    my @parts = $group eq 'ssl-shared' ? ('libssl') :
        ('libcrypto', 'providers/libdefault.a', 'providers/libcommon.a');
    for my $part (@parts) {
        for my $object (@{$unified_info{sources}{$part} // []}) {
            next if $object =~ /\.(?:a|ld)\z/;
            for my $source (@{$unified_info{sources}{$object} // []}) {
                my $path = abs_path($source) // die "missing source $source\n";
                my $stem = lc basename($path); $stem =~ s/\.(?:c|s)\z//;
                die "duplicate static object stem $stem\n" if exists $static_sources{$stem};
                $static_sources{$stem} = $path;
                $static_objects{$stem} = $object;
            }
        }
    }
}
my (%paths, %names, %per_file, %file_defines);
my @sources;
for my $object (@$objects) {
    next if $object =~ /\.(?:a|ld)\z/; # Providers are built by their own OS libraries.
    my $inputs = $unified_info{sources}{$object} // die "missing source for $object\n";
    die "ambiguous object source for $object\n" unless @$inputs == 1;
    my $file = $inputs->[0];
    die "unexpected source $file\n" unless $file =~ /\.(?:c|S|s)\z/;
    my $absolute = abs_path($file) // die "missing generated or source file $file\n";
    die "unsafe source path $absolute\n" if $absolute =~ /[^A-Za-z0-9_.\/+@%=-]/;
    my $name = basename($absolute);
    my $stem = lc $name; $stem =~ s/\.(?:c|s)\z//;
    if ($group =~ /-shared\z/ && exists $static_sources{$stem}) {
        die "conflicting shared object stem $stem\n" if $static_sources{$stem} ne $absolute;
        for my $kind (qw(defines includes)) {
            my $static = join("\0", @{$unified_info{$kind}{$static_objects{$stem}} // []});
            my $shared = join("\0", @{$unified_info{$kind}{$object} // []});
            die "different shared $kind for $stem\n" if $static ne $shared;
        }
        next;
    }
    die "duplicate object stem $stem\n" if $names{$stem}++;
    $paths{dirname($absolute)} = 1;
    push @sources, $name;
    $per_file{$name} = $unified_info{includes}{$object} // [];
    $file_defines{$name} = $unified_info{defines}{$object} // [];
}
print "# Generated from upstream OpenSSL configdata.pm; do not edit.\n";
print ".PATH: $_\n" for sort keys %paths;
if ($group =~ /-shared\z/) {
    for my $name (@sources) {
        (my $object = $name) =~ s/\.(?:c|S|s)\z/.pico/;
        print "SOBJS+= $object\n";
    }
} else {
    print "SRCS+= $_\n" for @sources;
}
sub include_flags {
    my ($values) = @_;
    my @out;
    for my $dir (@$values) {
        my $path = abs_path($dir) // die "missing include directory $dir\n";
        die "unsafe include path $path\n" if $path =~ /[^A-Za-z0-9_.\/+@%=-]/;
        push @out, "-I$path";
    }
    return join(' ', @out);
}
my $common_includes = include_flags($unified_info{includes}{$target} // []);
my $common_defines = $unified_info{defines}{$target} // [];
for my $name (@sources) {
    my $flags = join(' ', grep { length } $common_includes, include_flags($per_file{$name}));
    print "CPPFLAGS.$name+= $flags\n" if length $flags;
    for my $define (@$common_defines, @{$file_defines{$name}}) {
        die "unsafe define $define\n" unless $define =~ /\A[A-Za-z_][A-Za-z0-9_]*(?:=[A-Za-z0-9_.+-]+)?\z/;
        print "CPPFLAGS.$name+= -D$define\n";
    }
}

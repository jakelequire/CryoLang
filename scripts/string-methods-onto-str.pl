#!/usr/bin/env perl
# One-off codemod: moves calls of `string`'s text predicates onto `Str`.
#
#   RECEIVER.METHOD(ARGUMENT)  ->  Str::new(RECEIVER).METHOD(&Str::new(ARGUMENT))
#
# It is driven by the compiler's own diagnostics, never by a text search:
# build with the methods marked `internal` in a copy of the standard library,
# and every call on a `string` receiver is an E0353 naming its method at the
# method name's exact span. A `.contains(` on an Array or a Str is never
# reported, so it is never touched.
#
# usage: perl scripts/string-methods-onto-str.pl DIAGNOSTICS BASE METHOD...
#   DIAGNOSTICS  the build's output
#   BASE         the directory the build ran in (the spans are relative to it)
#   METHOD...    the methods to move; a site of any other method is left alone
#
# The receiver is found by walking back from the `.` over names, `.`, `::`
# and balanced brackets; the argument is the text between the call's
# balanced parentheses. A site whose text does not read as `.METHOD(` at its
# span stops the run before any file is written.
use strict;
use warnings;

my ($diagnostics, $base, @methods) = @ARGV;
die "usage: $0 DIAGNOSTICS BASE METHOD...\n" unless defined $base && @methods;
my %wanted = map { $_ => 1 } @methods;

open(my $dh, '<', $diagnostics) or die "$diagnostics: $!\n";
my %sites;    # file => [ [line, column, method], ... ]
my %seen;     # a span reported twice is one site
my $method;
while (my $text = <$dh>) {
    if ($text =~ /^error\[E0353\]: method `(\w+)` of `string` is internal/) {
        $method = $1;
        next;
    }
    if (defined $method && $text =~ /^\s*--> (.+):(\d+):(\d+)\s*$/) {
        push @{ $sites{"$base/$1"} }, [ $2, $3, $method ]
            if $wanted{$method} && !$seen{"$1:$2:$3"}++;
        undef $method;
    }
}
close($dh);

sub matching_open {
    my ($t, $i) = @_;
    my $close = substr($t, $i, 1);
    my $open = $close eq ')' ? '(' : '[';
    my $depth = 0;
    for (my $k = $i; $k >= 0; $k--) {
        my $c = substr($t, $k, 1);
        $depth++ if $c eq $close;
        $depth-- if $c eq $open;
        return $k if $depth == 0;
    }
    die "unbalanced `$close` at column " . ($i + 1) . ": $t";
}

sub matching_close {
    my ($t, $i) = @_;
    my $depth = 0;
    for (my $k = $i; $k < length($t); $k++) {
        my $c = substr($t, $k, 1);
        if ($c eq '"') {
            $k++;
            while ($k < length($t) && substr($t, $k, 1) ne '"') {
                $k++ if substr($t, $k, 1) eq '\\';
                $k++;
            }
            next;
        }
        $depth++ if $c eq '(';
        $depth-- if $c eq ')';
        return $k if $depth == 0;
    }
    die "unbalanced `(` at column " . ($i + 1) . ": $t";
}

sub rewrite_site {
    my ($t, $column, $name) = @_;
    my $at = $column - 1;
    my $dot = $at - 1;
    die "no `.$name(` at column $column: $t"
        unless substr($t, $dot, 1) eq '.'
            && substr($t, $at, length($name) + 1) eq "$name(";
    my $i = $dot - 1;
    while ($i >= 0) {
        my $c = substr($t, $i, 1);
        if ($c eq ')' || $c eq ']') { $i = matching_open($t, $i) - 1; next; }
        if ($c =~ /\w/ || $c eq '.') { $i--; next; }
        if ($c eq ':' && $i > 0 && substr($t, $i - 1, 1) eq ':') { $i -= 2; next; }
        last;
    }
    my $receiver_start = $i + 1;
    my $receiver = substr($t, $receiver_start, $dot - $receiver_start);
    die "empty receiver at column $column: $t" if $receiver eq '';
    my $open = $at + length($name);
    my $close = matching_close($t, $open);
    my $argument = substr($t, $open + 1, $close - $open - 1);
    my $call = "Str::new($receiver).$name(&Str::new($argument))";
    return substr($t, 0, $receiver_start) . $call . substr($t, $close + 1);
}

my $count = 0;
my %rewritten;
for my $file (sort keys %sites) {
    open(my $fh, '<', $file) or die "$file: $!\n";
    my @lines = <$fh>;
    close($fh);
    # Right to left on a line, so a rewrite never moves a column still to come.
    for my $site (sort { $a->[0] <=> $b->[0] or $b->[1] <=> $a->[1] } @{ $sites{$file} }) {
        my ($line, $column, $name) = @$site;
        $lines[$line - 1] = rewrite_site($lines[$line - 1], $column, $name);
        $count++;
    }
    $rewritten{$file} = \@lines;
}
for my $file (sort keys %rewritten) {
    open(my $fh, '>', $file) or die "$file: $!\n";
    print $fh @{ $rewritten{$file} };
    close($fh);
}
print "rewrote $count call sites in " . scalar(keys %rewritten) . " files\n";

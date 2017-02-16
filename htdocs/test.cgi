#!/usr/bin/perl

use strict;
use CGI;

my($cgi) = new CGI;

print $cgi->header('text/html');
print "Hello World!\n";

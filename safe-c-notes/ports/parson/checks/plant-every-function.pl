# inserts a double free as the first statement of every untrusted function
my @L=<STDIN>; my $n=0; my @P;
for my $i (0..$#L){ my $l=$L[$i]; 
 if ($l =~ /^ ?[A-Za-z_].*\)\s*\{?\s*$/ && $l !~ /^(_Trusted|typedef|struct|enum|#|PARSON_)/ && ($l=~/\{\s*$/ || $L[$i+1]=~/^\{/)) {
   my $j = $l=~/\{\s*$/ ? $i : $i+1; 
   my $prev=$L[$i-1]; next if $prev =~ /_Trusted/;
   $n++; $L[$j] =~ s/\n/ { char *_Owned _Nullable zz = malloc(1); free(zz); free(zz); }\n/; push @P, $j+1; } }
print @L; print STDERR join("\n",@P),"\n";

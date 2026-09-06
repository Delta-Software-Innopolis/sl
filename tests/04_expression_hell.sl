routine main() =
  var a : integer = 2;
  var b : integer = 3;
  var c : integer = 4;
  var d : integer = 5;

  print_integer(a + b * c);
  print_integer((a + b) * c);
  print_integer(- -a);
  print_integer(-(a + b));
  print_integer(c % b + d / a);
  print_boolean(a < b and c > d or not (a = b));
  print_integer(c - (d + b * (((a + b) * (c - a)) / (b - a) % d)));

  var x : real = 2.5;
  print_real(x * a + b / a);
  print_boolean(true or false and false);
  print_boolean(true xor true xor false);
end

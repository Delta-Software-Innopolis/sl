routine fibonacci(n: integer) =
  var n = 20;

  var prev = 0;
  var cur = 1;

  for i in 2..n loop
    var tmp = prev;
    prev := cur;
    cur := tmp + cur;
    print_integer(cur);
  end
end

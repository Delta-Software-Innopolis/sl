type i32 = integer
type Flag = boolean

var global_counter: integer = 100

routine increment_global() =
  global_counter := global_counter + 1
  print_integer(global_counter)
end

routine main() =
  var a: integer = 1;
  print_integer(a)

  if true then
    var a: integer = 2
    print_integer(a)
  end
  print_integer(a)

  for a in 1..3 loop
    print_integer(a)
  end
  print_integer(a)

  var inferred = 3.14
  print_real(inferred)
  var inferred_flag = true
  print_boolean(inferred_flag)

  var code: i32 = 1234
  print_integer(code)
  var is_ready: Flag = false
  print_boolean(is_ready)

  print_integer(global_counter)
  global_counter := global_counter + 1
  print_integer(global_counter)
  increment_global()
  print_integer(global_counter)
end

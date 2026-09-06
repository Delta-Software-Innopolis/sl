routine sum_array(arr : array[] integer) : integer =
  var total: integer = 0
  for x in arr loop
    total := total + x
  end
  return total
end

routine fill_doubled(arr : array[] integer) =
  for i in 0..(arr.size - 1) loop
    arr[i] := i * 2
  end
end

routine count_true(flags : array[] boolean) : integer =
  var count: integer = 0
  for f in flags loop
    if f then
      count := count + 1
    end
  end
  return count
end

routine main() =
  var numbers: array[5] integer
  for i in 0..4 loop
    numbers[i] := i + 1
  end
  for n in numbers loop
    print_integer(n)
  end
  print(sum_array(numbers))

  fill_doubled(numbers)
  for n in numbers loop
    print_integer(n)
  end

  var flags: array[4] boolean
  flags[0] := true
  flags[1] := false
  flags[2] := true
  flags[3] := true
  print_integer(count_true(flags))

  var matrix: array[2] array[3] integer
  for i in 0..1 loop
    for j in 0..2 loop
      matrix[i][j] := i * 3 + j
    end
  end
  for i in 0..1 loop
    for j in 0..2 loop
      print_integer(matrix[i][j])
    end
  end

  var alias: array[5] integer = numbers
  alias[0] := 999
  print_integer(numbers[0])
end

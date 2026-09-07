type IntArray = array [8] integer

routine merge(
  a: IntArray,
  temp: IntArray,
  left: integer,
  middle: integer,
  right: integer
) =
  var i: integer = left
  var j: integer = middle + 1
  var k: integer = left

  while (i <= middle) and (j <= right) loop
    if a[i] <= a[j] then
      temp[k] := a[i]
      i := i + 1
    else
      temp[k] := a[j]
      j := j + 1
    end

    k := k + 1
  end

  while i <= middle loop
    temp[k] := a[i]
    i := i + 1
    k := k + 1
  end

  while j <= right loop
    temp[k] := a[j]
    j := j + 1
    k := k + 1
  end

  k := left

  while k <= right loop
    a[k] := temp[k]
    k := k + 1
  end
end


routine merge_sort(
  a: IntArray,
  temp: IntArray,
  left: integer,
  right: integer
) =
  if left < right then
    var middle: integer = (left + right) / 2

    merge_sort(a, temp, left, middle)
    merge_sort(a, temp, middle + 1, right)

    merge(a, temp, left, middle, right)
  end
end


routine bubble_sort(a: IntArray, size: integer) =
  for i in 0 .. size - 2 loop
    for j in 0 .. size - i - 2 loop
      if a[j] > a[j + 1] then
        var temp: integer = a[j]

        a[j] := a[j + 1]
        a[j + 1] := temp
      end
    end
  end
end


routine main() =
  var merge_array: IntArray
  var merge_temp: IntArray
  var bubble_array: IntArray

  merge_array[0] := 5
  merge_array[1] := -2
  merge_array[2] := 8
  merge_array[3] := 1
  merge_array[4] := 5
  merge_array[5] := 0
  merge_array[6] := 3
  merge_array[7] := -4

  bubble_array[0] := 5
  bubble_array[1] := -2
  bubble_array[2] := 8
  bubble_array[3] := 1
  bubble_array[4] := 5
  bubble_array[5] := 0
  bubble_array[6] := 3
  bubble_array[7] := -4

  merge_sort(merge_array, merge_temp, 0, 7)
  bubble_sort(bubble_array, 8)

  for value in merge_array loop
    print_integer(value)
  end

  for value in bubble_array loop
    print_integer(value)
  end
end
type Student = record
  var id: integer
  var age: integer
  var gpa: real
end

type StudentArray = array [6] Student


routine print_student(student: Student) =
  print_integer(student.id)
  print_integer(student.age)
  print_real(student.gpa)
end


routine find_best_student(students: StudentArray): integer =
  var best_index: integer = 0

  for i in 1 .. 5 loop
    if students[i].gpa > students[best_index].gpa then
      best_index := i
    end
  end

  return best_index
end


routine average_gpa(students: StudentArray): real =
  var sum: real = 0.0

  for student in students loop
    sum := sum + student.gpa
  end

  return sum / 6.0
end


routine partition(
  students: StudentArray,
  low: integer,
  high: integer
): integer =
  var pivot: real = students[high].gpa
  var i: integer = low - 1

  for j in low .. high - 1 loop
    if students[j].gpa > pivot then
      i := i + 1

      var temp: Student = students[i]
      students[i] := students[j]
      students[j] := temp
    end
  end

  var temp: Student = students[i + 1]
  students[i + 1] := students[high]
  students[high] := temp

  return i + 1
end


routine quick_sort(
  students: StudentArray,
  low: integer,
  high: integer
) =
  if low < high then
    var pivot_index: integer = partition(students, low, high)

    quick_sort(students, low, pivot_index - 1)
    quick_sort(students, pivot_index + 1, high)
  end
end


routine print_above_gpa(
  students: StudentArray,
  bound: real
) =
  for student in students loop
    if student.gpa > bound then
      print_student(student)
    end
  end
end


routine main() =
  var students: StudentArray

  students[0].id := 101
  students[0].age := 20
  students[0].gpa := 3.2

  students[1].id := 102
  students[1].age := 19
  students[1].gpa := 4.0

  students[2].id := 103
  students[2].age := 22
  students[2].gpa := 2.7

  students[3].id := 104
  students[3].age := 21
  students[3].gpa := 3.8

  students[4].id := 105
  students[4].age := 20
  students[4].gpa := 3.5

  students[5].id := 106
  students[5].age := 23
  students[5].gpa := 3.8


  var best_index: integer = find_best_student(students)

  print_student(students[best_index])


  var avg: real = average_gpa(students)
  print_real(avg)


  quick_sort(students, 0, 5)

  for student in students loop
    print_student(student)
  end


  print_above_gpa(students, 3.6)
end
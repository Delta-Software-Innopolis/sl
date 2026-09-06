var n : integer = 8
var solutions : integer = 0
var cols : array[8] boolean
var diag1 : array[15] boolean
var diag2 : array[15] boolean

routine place(row : integer) =
  if row = n then
    solutions := solutions + 1
  else
    var col : integer = 0
    while col < n loop
      if not cols[col] and not diag1[row + col] and not diag2[row - col + n - 1] then
        cols[col] := true
        diag1[row + col] := true
        diag2[row - col + n - 1] := true

        place(row + 1)

        cols[col] := false
        diag1[row + col] := false
        diag2[row - col + n - 1] := false
      end
      col := col + 1
    end
  end
end

routine main() =
  place(0)
  print_integer(solutions)
end

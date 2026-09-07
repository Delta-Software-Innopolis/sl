routine factorial(n: integer): integer =
    if n <= 1 then
        return 1
    else
        return n * factorial(n - 1)
    end
end

routine main() =
    var n: integer = 5
    print_integer(factorial(n))
    print_integer(factorial(0))
    print_integer(factorial(3))
end


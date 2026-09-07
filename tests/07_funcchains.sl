routine third(a: integer, b: integer): integer

routine second(x: integer, y: integer): integer =
    var from_third: integer

    from_third := third(y, x)

    return from_third + x
end

routine first(p: integer, q: integer): integer =
    var from_second: integer

    from_second := second(p + 1, q + 2)

    return from_second - q
end

routine third(a: integer, b: integer): integer =
    var result: integer

    result := a * 10
    result := result + b

    return result
end

routine main() =
    var result: integer

    result := first(3, 4)

    print_integer(result)
end

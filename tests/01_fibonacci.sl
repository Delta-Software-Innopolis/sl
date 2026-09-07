routine main(n: integer) =
    var prev: integer = 0
    var cur: integer = 1

    print_integer(prev)
    print_integer(cur)

    for i in 2..n loop
        var tmp: integer = prev
        prev := cur
        cur := tmp + cur
        print_integer(cur)
    end
end
